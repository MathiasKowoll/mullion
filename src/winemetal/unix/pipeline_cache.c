/*
 * A cache of the GPU code of the pipelines Mullion makes, one MTLBinaryArchive per
 * pipeline.
 *
 * Mullion caches its own translation (shaders_<metal>.db) and points Metal's shader
 * cache at its folder, but Metal keeps at most about 1 GB of GPU code there: Phantom
 * Blade Zero's com.apple.metal stayed at 1.0 GB after its first run, and each launch
 * compiled its 80,000 pipelines again, for minutes.
 *
 * Metal inflates a classic archive opened from a file into ordinary memory, about
 * twenty times its size for PBZ's pipelines, so no archive stays open. A library
 * made from data is tagged with a hash of its bytes and a function with its
 * library's tag, its name and the constants' indices and types; a pipeline's key is
 * a hash of its functions' tags. A pipeline is first made with only the archives
 * filed under its key and MTLPipelineOptionFailOnBinaryArchiveMiss, and they are
 * let go; missed, it is compiled once into an archive of its own, made from it and
 * the archive written,
 * pipelines/<GPU>-<OS build>/<xx>/<key>-<pid>-<n>.bin beside Mullion's cache, off
 * the calling thread. The key leaves out pipeline state: Metal matches the whole
 * pipeline in the archives, so a key several pipelines share costs only another
 * file to open. What is written stops at MULLION_PIPELINE_CACHE_MAX_MB (4096).
 * MULLION_PIPELINE_CACHE=0 turns it off.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <CommonCrypto/CommonDigest.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "pipeline_cache.h"

extern NSString *MTLGetShaderCachePath(void);

static char tag_key;
static pthread_once_t setup_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t index_mutex = PTHREAD_MUTEX_INITIALIZER;
static NSMutableDictionary *index_files; /* key -> NSMutableArray of paths; under index_mutex */
static NSString *directory;               /* nil when the cache is off */
static dispatch_queue_t keep_queue;
static dispatch_group_t keep_group;
static atomic_uint written;
static atomic_ullong hits, misses, unkeyed, opened, kept_failed, kept_bytes, kept_count, over_cap;
static uint64_t cap_bytes;

static NSString *
sanitized(NSString *text) {
  NSCharacterSet *bad = [[NSCharacterSet alphanumericCharacterSet] invertedSet];
  return [[text componentsSeparatedByCharactersInSet:bad] componentsJoinedByString:@"_"];
}

static void
report(void) {
  if (!directory)
    return;
  dispatch_group_wait(keep_group, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
  fprintf(stderr,
          "mullion: pipeline cache: %llu found, %llu compiled, %llu kept (%.1f MB, %.0f KB each), %llu not kept over the "
          "%llu MB cap, %llu not keyed, %llu archives opened\n",
          (unsigned long long)hits, (unsigned long long)misses, (unsigned long long)kept_count, kept_bytes / 1e6,
          kept_count ? kept_bytes / 1e3 / kept_count : 0.0, (unsigned long long)over_cap,
          (unsigned long long)(cap_bytes >> 20), (unsigned long long)unkeyed, (unsigned long long)opened);
}

/* The folder, beside the one Mullion gave Metal's cache, and what is in it. */
static void
setup(void) {
  const char *setting = getenv("MULLION_PIPELINE_CACHE");
  if (setting && !strcmp(setting, "0"))
    return;
  @autoreleasepool {
    NSString *base = MTLGetShaderCachePath();
    if (base.length)
      base = [base stringByDeletingLastPathComponent];
    else {
      char buf[PATH_MAX];
      if (!confstr(_CS_DARWIN_USER_CACHE_DIR, buf, sizeof(buf)))
        return;
      base = [[NSString stringWithUTF8String:buf] stringByAppendingPathComponent:@"mullion"];
    }
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    NSString *where = [NSString stringWithFormat:@"%@-%@", sanitized(device.name ?: @"GPU"),
                                                 sanitized([[NSProcessInfo processInfo] operatingSystemVersionString])];
    [device release];
    directory = [[[base stringByAppendingPathComponent:@"pipelines"] stringByAppendingPathComponent:where] retain];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil
                                                    error:nil];
    /* Spotlight would index every archive a first run writes, beside the game. */
    [[NSFileManager defaultManager]
        createFileAtPath:[[base stringByAppendingPathComponent:@"pipelines"] stringByAppendingPathComponent:@".metadata_never_index"]
                contents:nil
              attributes:nil];
    index_files = [[NSMutableDictionary alloc] init];
    uint64_t files = 0, bytes = 0;
    NSDirectoryEnumerator *walk = [[NSFileManager defaultManager] enumeratorAtPath:directory];
    for (NSString *relative in walk) {
      NSString *file = relative.lastPathComponent;
      if (![file hasSuffix:@".bin"] || file.length < 33)
        continue;
      NSString *key = [file substringToIndex:32];
      NSMutableArray *paths = index_files[key];
      if (!paths)
        index_files[key] = paths = [NSMutableArray array];
      [paths addObject:[directory stringByAppendingPathComponent:relative]];
      bytes += walk.fileAttributes.fileSize;
      files++;
    }
    /* A first run of PBZ wrote 30 GB in minutes and filled the disk: what is kept stops at a cap. */
    const char *cap = getenv("MULLION_PIPELINE_CACHE_MAX_MB");
    cap_bytes = (uint64_t)(cap && atoll(cap) > 0 ? atoll(cap) : 4096) << 20;
    atomic_store(&kept_bytes, bytes);
    keep_queue = dispatch_get_global_queue(QOS_CLASS_UTILITY, 0);
    keep_group = dispatch_group_create();
    fprintf(stderr, "mullion: pipeline cache: %llu pipelines in %lu keys (%.1f MB of %llu MB) in %s\n",
            (unsigned long long)files, (unsigned long)index_files.count, bytes / 1e6,
            (unsigned long long)(cap_bytes >> 20), directory.UTF8String);
    atexit(report);
  }
}

static void
set_tag(id object, const unsigned char *digest) {
  NSData *tag = [[NSData alloc] initWithBytes:digest length:CC_SHA256_DIGEST_LENGTH];
  objc_setAssociatedObject(object, &tag_key, tag, OBJC_ASSOCIATION_RETAIN);
  [tag release];
}

void
pc_tag_library(id library, dispatch_data_t data) {
  pthread_once(&setup_once, setup);
  if (!directory || !library || !data)
    return;
  __block CC_SHA256_CTX context;
  CC_SHA256_Init(&context);
  dispatch_data_apply(data, ^bool(dispatch_data_t region, size_t offset, const void *bytes, size_t size) {
    (void)region;
    (void)offset;
    CC_SHA256_Update(&context, bytes, (CC_LONG)size);
    return true;
  });
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256_Final(digest, &context);
  set_tag(library, digest);
}

void
pc_tag_function(id function, id library, unsigned char kind, const char *name, const void *extra, size_t extra_size) {
  if (!directory || !function || !library)
    return;
  NSData *library_tag = objc_getAssociatedObject(library, &tag_key);
  if (!library_tag)
    return;
  CC_SHA256_CTX context;
  CC_SHA256_Init(&context);
  CC_SHA256_Update(&context, library_tag.bytes, (CC_LONG)library_tag.length);
  CC_SHA256_Update(&context, &kind, 1);
  CC_SHA256_Update(&context, name ? name : "", (CC_LONG)(name ? strlen(name) + 1 : 1));
  if (extra && extra_size)
    CC_SHA256_Update(&context, extra, (CC_LONG)extra_size);
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256_Final(digest, &context);
  set_tag(function, digest);
}

/* A pipeline's key from its functions, or nil when one has neither a tag nor a name. */
static NSString *
key_of(char kind, id<MTLFunction> const *functions, unsigned count) {
  CC_SHA256_CTX context;
  CC_SHA256_Init(&context);
  CC_SHA256_Update(&context, &kind, 1);
  unsigned present = 0;
  for (unsigned i = 0; i < count; i++) {
    unsigned char slot = (unsigned char)i;
    CC_SHA256_Update(&context, &slot, 1);
    if (!functions[i])
      continue;
    present++;
    NSData *tag = objc_getAssociatedObject(functions[i], &tag_key);
    if (tag) {
      CC_SHA256_Update(&context, tag.bytes, (CC_LONG)tag.length);
      continue;
    }
    /* Mullion's own functions, made from source, have no tag; their names stand in. */
    const char *name = functions[i].name.UTF8String;
    if (!name)
      return nil;
    CC_SHA256_Update(&context, name, (CC_LONG)strlen(name) + 1);
  }
  if (!present)
    return nil;
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256_Final(digest, &context);
  NSMutableString *text = [NSMutableString stringWithCapacity:32];
  for (unsigned i = 0; i < 16; i++)
    [text appendFormat:@"%02x", digest[i]];
  return text;
}

id
pc_make(id<MTLDevice> device, id descriptor, char kind, id<MTLFunction> const *functions, unsigned count,
        pc_make_block make, pc_add_block add, NSError **error) {
  pthread_once(&setup_once, setup);
  NSString *key = directory ? key_of(kind, functions, count) : nil;
  if (!key) {
    if (directory)
      atomic_fetch_add(&unkeyed, 1);
    return make(descriptor, MTLPipelineOptionNone, error);
  }
  NSArray *paths = nil;
  pthread_mutex_lock(&index_mutex);
  paths = [[index_files[key] copy] autorelease];
  pthread_mutex_unlock(&index_mutex);
  if (paths.count) {
    id state = nil;
    @autoreleasepool {
      NSMutableArray *archives = [NSMutableArray arrayWithCapacity:paths.count];
      for (NSString *path in paths) {
        MTLBinaryArchiveDescriptor *source = [[MTLBinaryArchiveDescriptor alloc] init];
        source.url = [NSURL fileURLWithPath:path];
        id<MTLBinaryArchive> archive = [device newBinaryArchiveWithDescriptor:source error:nil];
        [source release];
        if (!archive)
          continue;
        [archives addObject:archive];
        [archive release];
      }
      atomic_fetch_add(&opened, archives.count);
      if (archives.count) {
        id mine = [descriptor copy];
        [mine setBinaryArchives:archives];
        NSError *missed = nil;
        state = make(mine, MTLPipelineOptionFailOnBinaryArchiveMiss, &missed);
        [mine release];
      }
    }
    if (state) {
      atomic_fetch_add(&hits, 1);
      return state;
    }
  }
  /* A miss is compiled once, into an archive of its own, and made from it: compiling the
   * pipeline and then adding it compiled everything twice, and a first run took minutes more. */
  atomic_fetch_add(&misses, 1);
  id<MTLBinaryArchive> archive = nil;
  id state = nil;
  if (atomic_load(&kept_bytes) < cap_bytes) {
    MTLBinaryArchiveDescriptor *empty = [[MTLBinaryArchiveDescriptor alloc] init];
    archive = [device newBinaryArchiveWithDescriptor:empty error:nil];
    [empty release];
    NSError *failure = nil;
    if (archive && add(archive, descriptor, &failure)) {
      id mine = [descriptor copy];
      [mine setBinaryArchives:@[archive]];
      NSError *missed = nil;
      state = make(mine, MTLPipelineOptionFailOnBinaryArchiveMiss, &missed);
      [mine release];
    } else if (atomic_fetch_add(&kept_failed, 1) < 5)
      fprintf(stderr, "mullion: pipeline cache: keeping a pipeline failed: %s\n",
              failure.localizedDescription.UTF8String ?: "unknown");
    if (!state) {
      [archive release];
      archive = nil;
    }
  } else
    atomic_fetch_add(&over_cap, 1);
  if (!state)
    state = make(descriptor, MTLPipelineOptionNone, error);
  if (!state || !archive) {
    [archive release];
    return state;
  }
  [key retain];
  dispatch_group_async(keep_group, keep_queue, ^{
    @autoreleasepool {
      NSError *failure = nil;
      NSString *folder = [directory stringByAppendingPathComponent:[key substringToIndex:2]];
      NSString *path = [folder stringByAppendingPathComponent:[NSString stringWithFormat:@"%@-%d-%u.bin", key, getpid(),
                                                                         atomic_fetch_add(&written, 1)]];
      [[NSFileManager defaultManager] createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil
                                                      error:nil];
      if ([archive serializeToURL:[NSURL fileURLWithPath:path] error:&failure]) {
        uint64_t size = [[[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil] fileSize];
        atomic_fetch_add(&kept_bytes, size);
        if (atomic_fetch_add(&kept_count, 1) % 1000 == 999)
          fprintf(stderr, "mullion: pipeline cache: %llu kept, %.1f MB, %.0f KB each\n",
                  (unsigned long long)kept_count, kept_bytes / 1e6, kept_bytes / 1e3 / kept_count);
        pthread_mutex_lock(&index_mutex);
        NSMutableArray *list = index_files[key];
        if (!list)
          index_files[key] = list = [NSMutableArray array];
        [list addObject:path];
        pthread_mutex_unlock(&index_mutex);
      } else if (atomic_fetch_add(&kept_failed, 1) < 5)
        fprintf(stderr, "mullion: pipeline cache: writing a pipeline failed: %s\n",
                failure.localizedDescription.UTF8String ?: "unknown");
    }
    [archive release];
    [key release];
  });
  return state;
}

#ifndef MULLION_PIPELINE_CACHE_H
#define MULLION_PIPELINE_CACHE_H

#import <Metal/Metal.h>

/* Makes a pipeline from a descriptor with the options given, +1 retained, or nil. */
typedef id (^pc_make_block)(id descriptor, MTLPipelineOption options, NSError **error);
/* Adds a descriptor's pipeline to an archive. */
typedef BOOL (^pc_add_block)(id<MTLBinaryArchive> archive, id descriptor, NSError **error);

void pc_tag_library(id library, dispatch_data_t data);
void pc_tag_function(id function, id library, unsigned char kind, const char *name, const void *extra,
                     size_t extra_size);
/* make through the cache: found in this pipeline's archives, or made and kept. */
id pc_make(id<MTLDevice> device, id descriptor, char kind, id<MTLFunction> const *functions, unsigned count,
           pc_make_block make, pc_add_block add, NSError **error);

#endif

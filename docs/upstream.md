# Upstream

This fork (github.com/MathiasKowoll/mullion) follows two projects: DXMT, which Mullion is built on, and Mullion itself
(github.com/dttdrv/mullion). This file lists what has been taken from each, what was left and why, and how far each
was reviewed, so the next review starts where the last one stopped.

Mullion imported DXMT as one commit, with no history in common, so DXMT's changes cannot be merged: each is ported by
hand, and its commit message names the DXMT commit it ports. Mullion's own commits are cherry-picked.

## DXMT

- Base: [`7c8dee1`](https://github.com/3Shain/dxmt/commit/7c8dee1c2d73415301ceb7d1fa810861cef4cd67) (16 September
  2026), as Mullion's README says.
- Listed up to: [`3b25259`](https://github.com/3Shain/dxmt/commit/3b252590cb0290e36fca6f088b5255b871d2d95a)
  (27 October 2026), 121 commits after the base. On 31 October 2026, 13 are ported and 21 were already done in Mullion.

DXMT now writes a Direct3D 12 and a DXIL front end of its own, beside Mullion's, which were written independently.
Those commits are a reference for what Mullion lacks (DXMT's tessellation pipeline, for one), not patches. Each other
commit is reviewed: ported, already done in Mullion, or not applicable, with the reason.

States: *ported as `<commit>`*, *already in Mullion (`<commit>`)*, *reference*, *not applicable: <why>*, *in progress*,
*to review*.

| DXMT commit | Date | Subject | State |
|---|---|---|---|
| [`c5f854c`](https://github.com/3Shain/dxmt/commit/c5f854c) | 2026-09-28 | ci: add `_LIBCPP_KEEP_TRANSITIVE_INCLUDES_LLVM23` macro to llvm build | not applicable: DXMT's CI |
| [`e86484e`](https://github.com/3Shain/dxmt/commit/e86484e) | 2026-09-28 | ci: disable benchmark in llvm build | not applicable: DXMT's CI |
| [`fb45156`](https://github.com/3Shain/dxmt/commit/fb45156) | 2026-09-30 | fix(dxmt): initialize `device_` before the command helpers that use it | already in Mullion (`a261070`) |
| [`830eb41`](https://github.com/3Shain/dxmt/commit/830eb41) | 2026-10-02 | fix(d3d11): raise vertex buffer binding count to `D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT` | already in Mullion (`ba708e2`) |
| [`f9beaea`](https://github.com/3Shain/dxmt/commit/f9beaea) | 2026-10-02 | fix(dxmt): pass mipped width & height to clear encoder | already in Mullion (`b38e350`): only the depth-stencil clear's pass is sized from it; a colour clear's takes the attachment's size |
| [`acda4f9`](https://github.com/3Shain/dxmt/commit/acda4f9) | 2026-10-02 | fix(d3d11): remove `d3d11_device_mutex::try_lock()` | ported as `88e872a` |
| [`a22700c`](https://github.com/3Shain/dxmt/commit/a22700c) | 2026-10-02 | fix: fix a couple of copy-paste bugs | ported as `0aab04e` (the domain shader's pass-through type was already set; the D3D12 part is DXMT's own Direct3D 12) |
| [`59eb4c1`](https://github.com/3Shain/dxmt/commit/59eb4c1) | 2026-10-02 | fix(nvngx): use correct nvngx error value (0xbad00000) | in progress on branch dlss-d3d12 |
| [`542ca17`](https://github.com/3Shain/dxmt/commit/542ca17) | 2026-10-02 | fix(nvngx): use 1.0 as default pre-exposure value | in progress on branch dlss-d3d12 |
| [`e4f868d`](https://github.com/3Shain/dxmt/commit/e4f868d) | 2026-10-02 | fix(d3d10): fix a wrong pointer assignment | already in Mullion (`0696ae6`) |
| [`0fff7ff`](https://github.com/3Shain/dxmt/commit/0fff7ff) | 2026-10-02 | fix(nvapi): copy build branch string with null terminator | ported as `4be2566` |
| [`ce79ad0`](https://github.com/3Shain/dxmt/commit/ce79ad0) | 2026-10-05 | fix(d3d11): fix some logic flaws in dilated motion vector handling | ported as `67968f1` |
| [`0b63990`](https://github.com/3Shain/dxmt/commit/0b63990) | 2026-10-05 | fix(d3d11): lock device in `Signal()` and `Wait()` | ported as `44e7507` |
| [`ae91269`](https://github.com/3Shain/dxmt/commit/ae91269) | 2026-10-05 | fix(airconv): fix some logic flaws | ported as `21c4fb0` (the other three fixes were already in Mullion: `b3d4f59`, `0696ae6`, `aba1962`) |
| [`a3d9005`](https://github.com/3Shain/dxmt/commit/a3d9005) | 2026-10-05 | fix(d3d12): bail out failed `ExtractMTLInputLayoutElements()` before using unintialized `num_elements` | reference: DXMT's own Direct3D 12 |
| [`68af85e`](https://github.com/3Shain/dxmt/commit/68af85e) | 2026-10-05 | fix(d3d11): handle legitimate null parameter in `Create{Blend\|Rasterizer}State()` | already in Mullion (`aba1962`) |
| [`e40c9d7`](https://github.com/3Shain/dxmt/commit/e40c9d7) | 2026-10-05 | feat(d3d11): allow copying between different texture types in `CopySubresourceRegion()` | already in Mullion (`19b11eb`) |
| [`e94c312`](https://github.com/3Shain/dxmt/commit/e94c312) | 2026-10-06 | fix(d3d11): avoid deadlock on reentrant `ResizeTarget()` | already in Mullion (`37eb721`: the window is resized before the lock is taken) |
| [`5beb3b2`](https://github.com/3Shain/dxmt/commit/5beb3b2) | 2026-10-06 | chore(d3d11): remove redundant code for calculate number of mips | already in Mullion (`dcddabe`) |
| [`baf79e0`](https://github.com/3Shain/dxmt/commit/baf79e0) | 2026-10-07 | fix(d3d12): fixed handling case for unknown format in `CheckFeatureSupport` | reference: DXMT's own Direct3D 12 |
| [`0013428`](https://github.com/3Shain/dxmt/commit/0013428) | 2026-10-07 | fix(d3d11, winemetal): fixed compilation when `DXMT_NO_PRIVATE_API` is used | ported as `f66a017` (Mullion's blend state has no logic operation fallback to fix) |
| [`186a232`](https://github.com/3Shain/dxmt/commit/186a232) | 2026-10-07 | fix(d3d11): `pMappedResource` argument in `ID3D11DeviceContext::Map` can be NULL | already in Mullion (`dd3a840`, `338dce3`) |
| [`50102f9`](https://github.com/3Shain/dxmt/commit/50102f9) | 2026-10-16 | fix(nvngx): allow resources stored as `void *` to be read as D3D resources | in progress on branch dlss-d3d12 |
| [`5c80192`](https://github.com/3Shain/dxmt/commit/5c80192) | 2026-10-22 | refactor(airconv): introduce `VertexPuller` | not applicable: a refactor (`VertexPuller`) that prepares DXMT's DXIL front end, no DXBC fix |
| [`4b255b7`](https://github.com/3Shain/dxmt/commit/4b255b7) | 2026-10-22 | refactor(airconv): introduce `IOHelper` | not applicable: a refactor (`IOHelper`) that prepares DXMT's DXIL front end, no DXBC fix |
| [`39625fa`](https://github.com/3Shain/dxmt/commit/39625fa) | 2026-10-22 | fix(airconv): fix potential leaks of `sm50_*` objects | ported as `1703ca9` |
| [`35bfdb1`](https://github.com/3Shain/dxmt/commit/35bfdb1) | 2026-10-22 | feat(airconv): add kernel id intrinsics | reference: DXMT's own DXIL front end |
| [`f2fd9e9`](https://github.com/3Shain/dxmt/commit/f2fd9e9) | 2026-10-22 | feat(airconv): recognize dxil blob | reference: DXMT's own DXIL front end |
| [`4219e15`](https://github.com/3Shain/dxmt/commit/4219e15) | 2026-10-22 | feat(d3d12): pass dxil shader to airconv | reference: DXMT's own Direct3D 12 |
| [`47476d2`](https://github.com/3Shain/dxmt/commit/47476d2) | 2026-10-22 | chore(airconv): replace `handleAllErrors()` with `toString()` | already in Mullion (`0696ae6`, `ebf8eea`: every error is logged, whatever its kind) |
| [`7921d6b`](https://github.com/3Shain/dxmt/commit/7921d6b) | 2026-10-22 | feat(airconv): introduce `MetadataReader` | reference: DXMT's own DXIL front end |
| [`f108810`](https://github.com/3Shain/dxmt/commit/f108810) | 2026-10-22 | feat(airconv): parse dxil compute shader | reference: DXMT's own DXIL front end |
| [`8196329`](https://github.com/3Shain/dxmt/commit/8196329) | 2026-10-22 | feat(airconv): introduce dxil `Converter` | reference: DXMT's own DXIL front end |
| [`6871952`](https://github.com/3Shain/dxmt/commit/6871952) | 2026-10-22 | feat(airconv): implement dxil `createHandle` | reference: DXMT's own DXIL front end |
| [`c9126a7`](https://github.com/3Shain/dxmt/commit/c9126a7) | 2026-10-22 | feat(airconv): initialize dxil shader resource map | reference: DXMT's own DXIL front end |
| [`9166462`](https://github.com/3Shain/dxmt/commit/9166462) | 2026-10-22 | feat(airconv): implement dxil `cbufferLoadLegacy` | reference: DXMT's own DXIL front end |
| [`7336b95`](https://github.com/3Shain/dxmt/commit/7336b95) | 2026-10-22 | feat(airconv): implement dxil thread/group id intrinsics | reference: DXMT's own DXIL front end |
| [`33ea6ff`](https://github.com/3Shain/dxmt/commit/33ea6ff) | 2026-10-22 | feat(airconv): implement dxil `bufferStore` | reference: DXMT's own DXIL front end |
| [`20e291b`](https://github.com/3Shain/dxmt/commit/20e291b) | 2026-10-22 | feat(airconv): implement dxil `bufferLoad` | reference: DXMT's own DXIL front end |
| [`6836a75`](https://github.com/3Shain/dxmt/commit/6836a75) | 2026-10-22 | feat(airconv): implement dxil `textureStore` | reference: DXMT's own DXIL front end |
| [`8be0d72`](https://github.com/3Shain/dxmt/commit/8be0d72) | 2026-10-22 | feat(airconv): implement dxil `textureLoad` | reference: DXMT's own DXIL front end |
| [`a32ddb3`](https://github.com/3Shain/dxmt/commit/a32ddb3) | 2026-10-22 | feat(airconv): implement dxil `bufferUpdateCounter` | reference: DXMT's own DXIL front end |
| [`4f1f313`](https://github.com/3Shain/dxmt/commit/4f1f313) | 2026-10-22 | feat(airconv): handle 16-bit resource descriptors | reference: DXMT's own DXIL front end |
| [`01e8ab3`](https://github.com/3Shain/dxmt/commit/01e8ab3) | 2026-10-22 | feat(d3d12): report `Native16BitShaderOpsSupported`=`TRUE` | reference: DXMT's own Direct3D 12 |
| [`130aefd`](https://github.com/3Shain/dxmt/commit/130aefd) | 2026-10-22 | feat(airconv): implement dxil `getDimensions` | reference: DXMT's own DXIL front end |
| [`e473584`](https://github.com/3Shain/dxmt/commit/e473584) | 2026-10-22 | feat(airconv): implement dxil `rawBufferStore` and `rawBufferLoad` | reference: DXMT's own DXIL front end |
| [`0a02fff`](https://github.com/3Shain/dxmt/commit/0a02fff) | 2026-10-22 | feat(airconv): implement dxil `legacyF16ToF32` and `legacyF32ToF16` | reference: DXMT's own DXIL front end |
| [`7061e6e`](https://github.com/3Shain/dxmt/commit/7061e6e) | 2026-10-22 | feat(airconv): implement dxil `unaryBits` | reference: DXMT's own DXIL front end |
| [`2249dab`](https://github.com/3Shain/dxmt/commit/2249dab) | 2026-10-22 | feat(airconv): support group shared memory and indexable constants | reference: DXMT's own DXIL front end |
| [`73573ab`](https://github.com/3Shain/dxmt/commit/73573ab) | 2026-10-22 | fix(d3d12): fail unsupported shader model query | reference: DXMT's own Direct3D 12 |
| [`23f2247`](https://github.com/3Shain/dxmt/commit/23f2247) | 2026-10-22 | feat(airconv): implement dxil `barrier` | reference: DXMT's own DXIL front end |
| [`cec11cb`](https://github.com/3Shain/dxmt/commit/cec11cb) | 2026-10-22 | refactor(airconv): store a fat `dxil::EntryPoint` object instead of a raw `llvm::Function` pointer | reference: DXMT's own DXIL front end |
| [`b8ab0d3`](https://github.com/3Shain/dxmt/commit/b8ab0d3) | 2026-10-22 | feat(airconv): parse dxil vertex & pixel shader | reference: DXMT's own DXIL front end |
| [`2bc394b`](https://github.com/3Shain/dxmt/commit/2bc394b) | 2026-10-22 | feat(airconv): implement dxil `loadInput` and `storeOutput` | reference: DXMT's own DXIL front end |
| [`297d279`](https://github.com/3Shain/dxmt/commit/297d279) | 2026-10-22 | feat(airconv): implement dxil `sample` | reference: DXMT's own DXIL front end |
| [`1f96b5e`](https://github.com/3Shain/dxmt/commit/1f96b5e) | 2026-10-22 | feat(airconv): handle 16-bit input and output | reference: DXMT's own DXIL front end |
| [`6eefe27`](https://github.com/3Shain/dxmt/commit/6eefe27) | 2026-10-22 | feat(airconv): handle 16-bit `SV_Position` | reference: DXMT's own DXIL front end |
| [`c7ff6c1`](https://github.com/3Shain/dxmt/commit/c7ff6c1) | 2026-10-22 | refactor(airconv): introduce `to_air_sample_type()` | reference: DXMT's own DXIL front end |
| [`d72b482`](https://github.com/3Shain/dxmt/commit/d72b482) | 2026-10-22 | feat(airconv): record texture gather usage | reference: DXMT's own DXIL front end |
| [`a04f6af`](https://github.com/3Shain/dxmt/commit/a04f6af) | 2026-10-22 | chore(d3d12): advertise `Int64ShaderOps` | reference: DXMT's own Direct3D 12 |
| [`cfc1647`](https://github.com/3Shain/dxmt/commit/cfc1647) | 2026-10-22 | feat(airconv): implement dxil `sample` variants | reference: DXMT's own DXIL front end |
| [`2baa9b4`](https://github.com/3Shain/dxmt/commit/2baa9b4) | 2026-10-22 | feat(airconv): implement dxil `textureGather` and `textureGatherCmp` | reference: DXMT's own DXIL front end |
| [`bf67ddc`](https://github.com/3Shain/dxmt/commit/bf67ddc) | 2026-10-22 | fix(d3d12): add atomic usage to eligible texture | reference: DXMT's own Direct3D 12 |
| [`17ac060`](https://github.com/3Shain/dxmt/commit/17ac060) | 2026-10-22 | feat(airconv): add more AIR trigonometric intrinsics | reference: DXMT's own DXIL front end |
| [`0268975`](https://github.com/3Shain/dxmt/commit/0268975) | 2026-10-22 | chore(airconv): define known dxil op constants | reference: DXMT's own DXIL front end |
| [`9f38e54`](https://github.com/3Shain/dxmt/commit/9f38e54) | 2026-10-22 | feat(airconv): implement dxil `unary` | reference: DXMT's own DXIL front end |
| [`e2ca44b`](https://github.com/3Shain/dxmt/commit/e2ca44b) | 2026-10-22 | feat(airconv): implement dxil `binary` | reference: DXMT's own DXIL front end |
| [`fe79b95`](https://github.com/3Shain/dxmt/commit/fe79b95) | 2026-10-22 | feat(airconv): implement dxil `tertiary` | reference: DXMT's own DXIL front end |
| [`e9cc1d0`](https://github.com/3Shain/dxmt/commit/e9cc1d0) | 2026-10-22 | feat(airconv): implement dxil dot products | reference: DXMT's own DXIL front end |
| [`9ec6613`](https://github.com/3Shain/dxmt/commit/9ec6613) | 2026-10-22 | feat(airconv): implement dxil `isSpecialFloat` | reference: DXMT's own DXIL front end |
| [`8f4dad4`](https://github.com/3Shain/dxmt/commit/8f4dad4) | 2026-10-22 | feat(airconv): handle llvm `atomicrmw` | reference: DXMT's own DXIL front end |
| [`f0e8a21`](https://github.com/3Shain/dxmt/commit/f0e8a21) | 2026-10-22 | feat(airconv): implement dxil `atomicBinOp` | reference: DXMT's own DXIL front end |
| [`1197857`](https://github.com/3Shain/dxmt/commit/1197857) | 2026-10-22 | feat(airconv): implement dxil `atomicCompareExchange` | reference: DXMT's own DXIL front end |
| [`7774bba`](https://github.com/3Shain/dxmt/commit/7774bba) | 2026-10-22 | feat(airconv): implement dxil `sampleGrad` | reference: DXMT's own DXIL front end |
| [`69233ed`](https://github.com/3Shain/dxmt/commit/69233ed) | 2026-10-22 | feat(airconv): implement dxil `discard` | reference: DXMT's own DXIL front end |
| [`a8e5b87`](https://github.com/3Shain/dxmt/commit/a8e5b87) | 2026-10-22 | feat(airconv): implement dxil `[earlydepthstencil]` | reference: DXMT's own DXIL front end |
| [`aa76581`](https://github.com/3Shain/dxmt/commit/aa76581) | 2026-10-22 | feat(airconv): implement dxil `SV_Depth` | reference: DXMT's own DXIL front end |
| [`5aee35a`](https://github.com/3Shain/dxmt/commit/5aee35a) | 2026-10-22 | feat(airconv): implement dxil `SV_IsFrontFace` | reference: DXMT's own DXIL front end |
| [`918df56`](https://github.com/3Shain/dxmt/commit/918df56) | 2026-10-22 | feat(airconv): parse dxil semantic indices | reference: DXMT's own DXIL front end |
| [`b856c46`](https://github.com/3Shain/dxmt/commit/b856c46) | 2026-10-22 | feat(airconv): support multi-row signature element | reference: DXMT's own DXIL front end |
| [`30d81b2`](https://github.com/3Shain/dxmt/commit/30d81b2) | 2026-10-22 | feat(airconv): implement dxil `calculateLOD` | reference: DXMT's own DXIL front end |
| [`aa11f0b`](https://github.com/3Shain/dxmt/commit/aa11f0b) | 2026-10-22 | feat(airconv): implement dxil `texture2DMSGetSamplePosition` | reference: DXMT's own DXIL front end |
| [`f2df8cf`](https://github.com/3Shain/dxmt/commit/f2df8cf) | 2026-10-22 | feat(airconv): implement dxil `SV_RenderTargetArrayIndex` and `SV_ViewportArrayIndex` | reference: DXMT's own DXIL front end |
| [`5979bc5`](https://github.com/3Shain/dxmt/commit/5979bc5) | 2026-10-22 | feat(airconv): implement dxil `SV_ClipDistance` and `SV_CullDistance` | reference: DXMT's own DXIL front end |
| [`f94c7cf`](https://github.com/3Shain/dxmt/commit/f94c7cf) | 2026-10-22 | feat(airconv): implement dxil `waveGetLaneIndex` and `waveGetLaneCount` | reference: DXMT's own DXIL front end |
| [`72ee0cd`](https://github.com/3Shain/dxmt/commit/72ee0cd) | 2026-10-22 | feat(airconv): implement dxil `quadReadLaneAt` and `waveReadLaneAt` | reference: DXMT's own DXIL front end |
| [`4b25328`](https://github.com/3Shain/dxmt/commit/4b25328) | 2026-10-22 | feat(airconv): implement dxil `quadOp` | reference: DXMT's own DXIL front end |
| [`12dcfee`](https://github.com/3Shain/dxmt/commit/12dcfee) | 2026-10-22 | feat(airconv): implement dxil `sampleIndex` | reference: DXMT's own DXIL front end |
| [`c6dd845`](https://github.com/3Shain/dxmt/commit/c6dd845) | 2026-10-22 | feat(airconv): implement dxil `coverage` | reference: DXMT's own DXIL front end |
| [`9ea4ec9`](https://github.com/3Shain/dxmt/commit/9ea4ec9) | 2026-10-22 | feat(airconv): implement dxil `SV_Coverage` output | reference: DXMT's own DXIL front end |
| [`9092131`](https://github.com/3Shain/dxmt/commit/9092131) | 2026-10-22 | feat(airconv): implement dxil `renderTargetGetSampleCount` | reference: DXMT's own DXIL front end |
| [`ce001df`](https://github.com/3Shain/dxmt/commit/ce001df) | 2026-10-22 | fix(airconv): handle OUTPUT_CONTROL_POINT_ID relative indexing | ported as `f3cbcfc` |
| [`a71b60e`](https://github.com/3Shain/dxmt/commit/a71b60e) | 2026-10-20 | fix(d3d11): take the device lock on the unlocked context entries | ported as `4640770` |
| [`e186434`](https://github.com/3Shain/dxmt/commit/e186434) | 2026-10-05 | feat(d3d11): implement `SwapDeviceContextState()` | already in Mullion (`0696ae6`) |
| [`a148782`](https://github.com/3Shain/dxmt/commit/a148782) | 2026-10-06 | feat(d3d11, dxgi): implement dxgi surface | already in Mullion (`0dbe78b`) |
| [`5209b37`](https://github.com/3Shain/dxmt/commit/5209b37) | 2026-10-26 | feat(airconv): parse dxil geometry shader | reference: DXMT's own DXIL front end |
| [`cae78f4`](https://github.com/3Shain/dxmt/commit/cae78f4) | 2026-10-26 | feat(airconv): implement dxil `emitStream` and `cutStream` | reference: DXMT's own DXIL front end |
| [`7a14a9a`](https://github.com/3Shain/dxmt/commit/7a14a9a) | 2026-10-26 | feat(airconv): implement dxil `loadInput` with vertex index in geometry shader | reference: DXMT's own DXIL front end |
| [`f13dfc7`](https://github.com/3Shain/dxmt/commit/f13dfc7) | 2026-10-26 | feat(airconv): implement dxil `SV_RenderTargetArrayIndex` and `SV_ViewportArrayIndex` in geometry shader | reference: DXMT's own DXIL front end |
| [`6d1bb98`](https://github.com/3Shain/dxmt/commit/6d1bb98) | 2026-10-26 | feat(airconv): implement dxil `SV_ClipDistance` and `SV_CullDistance` in geometry shader | reference: DXMT's own DXIL front end |
| [`e2ec8a9`](https://github.com/3Shain/dxmt/commit/e2ec8a9) | 2026-10-26 | fix(airconv): emit `SV_ClipDistance` and `SV_CullDistance` as vertex outputs in dxbc geometry shader | already in Mullion (`0696ae6`) |
| [`94d12d3`](https://github.com/3Shain/dxmt/commit/94d12d3) | 2026-10-26 | feat(airconv): implement dxil `gsInstanceID` | reference: DXMT's own DXIL front end |
| [`914ae9a`](https://github.com/3Shain/dxmt/commit/914ae9a) | 2026-10-26 | feat(airconv): implement gs with dynamic index buffer view | reference: index buffer views that DXMT's own Direct3D 12 sets from the GPU (Mullion's has `SM50_INDEX_BUFFER_FORMAT_VIEW`) |
| [`39a4722`](https://github.com/3Shain/dxmt/commit/39a4722) | 2026-10-26 | feat(d3d12): introduce `GetRenderPipelineVariant()` on render pipeline state object | reference: DXMT's own Direct3D 12 |
| [`9432f58`](https://github.com/3Shain/dxmt/commit/9432f58) | 2026-10-26 | feat(d3d12): implement render pipeline with geometry shader | reference: DXMT's own Direct3D 12 |
| [`7854fdf`](https://github.com/3Shain/dxmt/commit/7854fdf) | 2026-10-26 | fix(airconv): do handle `SV_GSInstanceID` in dxbc geometry shader | already in Mullion (`0696ae6`) |
| [`ff5ea68`](https://github.com/3Shain/dxmt/commit/ff5ea68) | 2026-10-26 | feat(airconv): implement dxil  & dxbc `SV_PrimitiveID` in geometry & pixel shader | already in Mullion for DXBC (`0696ae6`); the DXIL half is a reference |
| [`21734bc`](https://github.com/3Shain/dxmt/commit/21734bc) | 2026-10-26 | chore(airconv): remove unused compare-exchange temp register | ported as `336fe26` |
| [`12107ac`](https://github.com/3Shain/dxmt/commit/12107ac) | 2026-10-26 | fix(DXBCParser): remove assertion to allow dxbc container to not have `RTS0` chunk | already in Mullion (`9dc389f`) |
| [`82246f7`](https://github.com/3Shain/dxmt/commit/82246f7) | 2026-10-26 | feat(d3d12): report minimum feature level passed at device creation | reference: DXMT's own Direct3D 12 |
| [`1ee4988`](https://github.com/3Shain/dxmt/commit/1ee4988) | 2026-10-26 | feat(d3d12): implement `WriteBufferImmediate()` | reference: DXMT's own Direct3D 12 |
| [`1b9104a`](https://github.com/3Shain/dxmt/commit/1b9104a) | 2026-10-23 | fix(d3d12): handle heap allocation failure in `MTLD3D12HeapImpl::Initialize` | reference: DXMT's own Direct3D 12 |
| [`5cafc43`](https://github.com/3Shain/dxmt/commit/5cafc43) | 2026-10-26 | fix(winemetal): retain returned error | already in Mullion (`8a8b6ae`) |
| [`310d5d0`](https://github.com/3Shain/dxmt/commit/310d5d0) | 2026-10-26 | fix(winemetal): create a `MTLCompileOptions` for `newLibraryWithSource` | already in Mullion (`76363f2`) |
| [`a741a3f`](https://github.com/3Shain/dxmt/commit/a741a3f) | 2026-10-26 | fix(winemetal): fix memory leaks in `WMTGetDisplayDescription()` | ported as `4a9325a` |
| [`d815f7f`](https://github.com/3Shain/dxmt/commit/d815f7f) | 2026-10-26 | fix(winemetal): fix memory leaks in `WMTQueryDisplaySettingForLayer()` | ported as `6ed5079` |
| [`9839e55`](https://github.com/3Shain/dxmt/commit/9839e55) | 2026-10-26 | fix(airconv): store call return point before redirecting to branch | already in Mullion (`aba1962`) |
| [`77be3b9`](https://github.com/3Shain/dxmt/commit/77be3b9) | 2026-10-26 | feat(airconv): implement ts with dynamic index buffer view | reference: index buffer views that DXMT's own Direct3D 12 sets from the GPU (Mullion's has `SM50_INDEX_BUFFER_FORMAT_VIEW`) |
| [`a159fb3`](https://github.com/3Shain/dxmt/commit/a159fb3) | 2026-10-26 | feat(d3d12): implement tessellation render pipeline | reference: DXMT's own Direct3D 12 |
| [`d162ea4`](https://github.com/3Shain/dxmt/commit/d162ea4) | 2026-10-27 | fix(airconv): set point size and primitive count for gs pointlist output | already in Mullion (`0696ae6`) |
| [`3b25259`](https://github.com/3Shain/dxmt/commit/3b25259) | 2026-10-27 | fix(d3d12): remove double-free of `sm50_err` | reference: DXMT's own Direct3D 12 |

## Mullion (dttdrv/mullion)

- Base: [`695dbe2`](https://github.com/dttdrv/mullion/commit/695dbe2a6dfaf22ea74143e3e49ab39441f2c443) (30 October
  2026), upstream's main.
- Reviewed up to: `695dbe2` on main; 43 pull requests were open on 31 October 2026, all unmerged.

| Upstream | Taken as | State |
|---|---|---|
| PR [#101](https://github.com/dttdrv/mullion/pull/101) (`970d431`): airconv moves loads of device and constant memory ahead of their uses | `339b061` | cherry-picked |

## This fork's own commits

Commits on main that neither upstream has, oldest first.

| Commit | Subject |
|---|---|
| `224dfe9` | winemetal: keep each pipeline's GPU code in an archive of its own |
| `5c2d455` | winemetal: compile a missed pipeline once, and cap the pipeline cache |
| `b439baa` | winemetal: keep only the GPU's slice of each pipeline archive |
| `3b8beb5` | winemetal: remove the gpuarchiver folders the pipeline cache leaves |
| `1dea20e` | winemetal: drop the shader cache tables of other converter versions |
| `8a02f3f` | winemetal: raise the pipeline cache's default cap to 10 GB |
| `f725b18` | d3d12: time Metal 4 passes between encoders, in heaps of the profile's own |
| `103b121` | d3d12: a profile ends compute and copy passes at application events |
| `2a323ca` | tests: held.py checks the profile's passes against command buffer spans |
| `43f3bb2` | nvapi: a function it does not implement is named once |
| `8bd6b15` | d3d12: the recording arena's mappings begin where it begins |
| `0b71541` | nvapi: a GPU's adapter ID is its DXGI adapter's LUID |
| `84e4902` | d3d12: a refused state object says why, and leaves what it was made of (branch `rtpso-raygen`) |
| `b7b3043` | d3d12: a pipeline refused for its sample count says the count (branch `rtpso-raygen`) |
| `722ba52` | d3d12: a profile records what each frame's submissions wait on (branch `frame-profile`; DXMT has no profiler to map it to) |
| `00d2609` | tools: mullion-diag judges each frame by the GPU's idle time (branch `frame-profile`) |
| `7c239fa` | tests: held.py checks the frame verdict against frames of known bounds (branch `frame-profile`) |
| `6467544` | tests: the perf probe pairs ExecuteIndirect calls with the same work issued directly (branch `indirect-cost`) |
| `8649192` | d3d12: ExecuteIndirect of a few dispatches or draws runs them as Metal's own (branch `indirect-cost`) |

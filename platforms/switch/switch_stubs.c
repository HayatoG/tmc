/*
 * Switch-only support sources for the Minish Cap homebrew build.
 *
 * Currently just the link-stub for the one ViruaPPU symbol whose feature
 * patch we skip on the first Switch boot. See platforms/switch/Makefile.
 */
#include <stdint.h>

/*
 * Sub-pixel affine OAM overlay. The PC build's `viruappu-internal-scale`
 * patch adds the real implementation inside ViruaPPU; the Switch build does
 * not apply that patch (it conflicts with the pinned submodule commit), so
 * we link this no-op. At internal render scale 1 (the Switch default) the
 * real function is a no-op anyway, so rendering is unaffected. Revisit when
 * enabling internal upscaling on Switch.
 */
void virtuappu_mode1_render_affine_obj_overlay(uint32_t *dst, int dst_w,
                                               int dst_h, int scale) {
    (void)dst;
    (void)dst_w;
    (void)dst_h;
    (void)scale;
}

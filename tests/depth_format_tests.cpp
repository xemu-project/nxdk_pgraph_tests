#include "depth_format_tests.h"

#include <hal/xbox.h>
#include <pbkit/pbkit.h>
#include <windows.h>

#include "../shaders/precalculated_vertex_shader.h"
#include "../test_host.h"
#include "../texture_format.h"
#include "nxdk_ext.h"
#include "pbkit_ext.h"
#include "vertex_buffer.h"

constexpr uint32_t kDepthFormats[] = {
    NV097_SET_SURFACE_FORMAT_ZETA_Z24S8,
    NV097_SET_SURFACE_FORMAT_ZETA_Z16,
};

constexpr uint32_t kNumDepthFormats = sizeof(kDepthFormats) / sizeof(kDepthFormats[0]);

static SDL_Surface *generate_gradient_surface(uint32_t width, uint32_t height);

DepthFormatTests::DepthFormatTests(TestHost &host, std::string output_dir) : TestBase(host, std::move(output_dir)) {}

void DepthFormatTests::Run() {
  // NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8
  const TextureFormatInfo &texture_format = kTextureFormats[3];
  host_.SetTextureFormat(texture_format);

  auto shader = std::make_shared<PrecalculatedVertexShader>();
  host_.SetShaderProgram(shader);

  const uint32_t texture_width = host_.GetTextureWidth();
  const uint32_t texture_height = host_.GetTextureHeight();
  auto gradient_surface = generate_gradient_surface(texture_width, texture_height);
  if (!gradient_surface) {
    return;
  }

  int result = host_.SetTexture(gradient_surface);
  SDL_FreeSurface(gradient_surface);
  if (result) {
    return;
  }

  auto fb_width = static_cast<float>(host_.GetFramebufferWidth());
  auto fb_height = static_cast<float>(host_.GetFramebufferHeight());

  float left = 120.0f;
  float right = left + (fb_width - left * 2.0f);
  float top = 40;
  float bottom = top + (fb_height - top * 2.0f);
  uint32_t big_quad_width = static_cast<int>(right - left);
  uint32_t big_quad_height = static_cast<int>(bottom - top);

  constexpr uint32_t kSmallSize = 30;
  constexpr uint32_t kSmallSpacing = 15;
  constexpr uint32_t kStep = kSmallSize + kSmallSpacing;

  uint32_t quads_per_row = (big_quad_width - kSmallSize) / kStep;
  uint32_t quads_per_col = (big_quad_height - kSmallSize) / kStep;

  uint32_t row_size = kSmallSize + quads_per_row * kStep;
  uint32_t col_size = kSmallSize + quads_per_row * kStep;

  uint32_t x_offset = left + kSmallSpacing + (big_quad_width - row_size) / 2;
  uint32_t y_offset = top + kSmallSpacing + (big_quad_height - col_size) / 2;

  uint32_t num_quads = 1 + quads_per_row * quads_per_col;
  std::shared_ptr<VertexBuffer> buffer = host_.AllocateVertexBuffer(6 * num_quads);

  float z_inc = static_cast<float>(0xFFFF / (num_quads + 1));

  // Quads are intentionally rendered from front to back to verify the behavior of the depth buffer.
  uint32_t idx = 0;
  float z_left = 0.0f;
  float z_right = 0.0f;
  float y = y_offset;
  for (int y_idx = 0; y_idx < quads_per_col; ++y_idx, y += kStep) {
    float x = x_offset;
    for (int x_idx = 0; x_idx < quads_per_row; ++x_idx, z_left += z_inc, z_right *= -1.0f, x += kStep) {
      buffer->DefineQuad(idx++, x, y, x + kSmallSize, y + kSmallSize, z_left, z_left, z_right, z_right);
    }

    z_right *= 1.5f;
  }

  buffer->DefineQuad(idx++, left, top, right, bottom, 65535.0f);

  constexpr uint32_t depth_cutoffs[] = {
      0x00FFFFFF, 0x007FFFFF, 0x0007FFFF, 0x00007FFF, 0x000007FF, 0x0000007F, 0x00000007, 0x00000000,
  };

  for (auto depth_format : kDepthFormats) {
    for (auto cutoff : depth_cutoffs) {
      Test(depth_format, false, cutoff);
    }
  }
}

void DepthFormatTests::Test(uint32_t depth_format, bool compress_z, uint32_t depth_cutoff) {
  host_.SetDepthBufferFormat(depth_format);
  host_.PrepareDraw(0xFF000000, depth_cutoff, 0x00);

  auto p = pb_begin();
  p = pb_push1(p, NV097_SET_CONTROL0, MASK(NV097_SET_CONTROL0_Z_FORMAT, NV097_SET_CONTROL0_Z_FORMAT_FIXED) | 1);

  p = pb_push1(p, NV097_SET_DEPTH_TEST_ENABLE, true);
  p = pb_push1(p, NV097_SET_DEPTH_MASK, true);
  p = pb_push1(p, NV097_SET_DEPTH_FUNC, NV097_SET_DEPTH_FUNC_V_LESS);

  p = pb_push1(p, NV097_SET_COMPRESS_ZBUFFER_EN, compress_z);

  //#define CULL_NEAR_FAR_SHIFT 0
  //#define ZCLAMP_SHIFT 4
  //#define CULL_IGNORE_W_SHIFT 8
  //  uint32_t min_max_control = (1 << CULL_NEAR_FAR_SHIFT) | (1 << ZCLAMP_SHIFT) | (1 << CULL_IGNORE_W_SHIFT);
  //  p = pb_push1(p, NV097_SET_ZMIN_MAX_CONTROL, min_max_control);

  p = pb_push1(p, NV097_SET_STENCIL_TEST_ENABLE, false);
  p = pb_push1(p, NV097_SET_STENCIL_MASK, false);
  pb_end(p);

  host_.DrawVertices();

  pb_print("DF: %d\n", depth_format);
  pb_print("C: %x\n", depth_cutoff);
  pb_draw_text_screen();

  char buf[64] = {0};
  snprintf(buf, 63, "DepthFmt_DF_%d", depth_format);

  host_.FinishDrawAndSave(output_dir_.c_str(), buf);

  Sleep(100);
}

SDL_Surface *generate_gradient_surface(uint32_t texture_width, uint32_t texture_height) {
  SDL_Surface *gradient_surface = SDL_CreateRGBSurfaceWithFormat(
      0, static_cast<int>(texture_width), static_cast<int>(texture_height), 32, SDL_PIXELFORMAT_RGBA8888);
  if (!gradient_surface) {
    return gradient_surface;
  }

  if (SDL_LockSurface(gradient_surface)) {
    SDL_FreeSurface(gradient_surface);
    return gradient_surface;
  }

  auto pixels = static_cast<uint32_t *>(gradient_surface->pixels);
  for (int x = 0; x < texture_width; ++x) {
    int normalized_x = static_cast<int>(static_cast<float>(x) * 255.0f / static_cast<float>(texture_width));
    pixels[x] = SDL_MapRGB(gradient_surface->format, normalized_x, normalized_x, 64);
  }
  for (int y = 1; y < texture_height; ++y, pixels += texture_width) {
    memcpy(pixels + texture_width, pixels, texture_width * 4);
  }

  SDL_UnlockSurface(gradient_surface);
  return gradient_surface;
}

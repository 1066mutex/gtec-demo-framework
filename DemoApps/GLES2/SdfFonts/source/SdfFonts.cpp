/****************************************************************************************************************************************************
 * Copyright 2020, 2022-2025 NXP
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    * Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *
 *    * Redistributions in binary form must reproduce the above copyright notice,
 *      this list of conditions and the following disclaimer in the documentation
 *      and/or other materials provided with the distribution.
 *
 *    * Neither the name of the NXP. nor the names of
 *      its contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************************************************************************************/

#include "SdfFonts.hpp"
#include <FslBase/Log/IO/FmtPath.hpp>
#include <FslBase/Log/Log3Fmt.hpp>
#include <FslBase/Math/MathHelper.hpp>
#include <FslBase/Span/SpanUtil_Vector.hpp>
#include <FslBase/UncheckedNumericCast.hpp>
#include <FslDemoService/Graphics/IGraphicsService.hpp>
#include <FslGraphics/Color.hpp>
#include <FslGraphics/Render/Adapter/INativeBatch2D.hpp>
#include <FslUtil/OpenGLES2/Exceptions.hpp>
#include <FslUtil/OpenGLES2/GLCheck.hpp>
#include <Shared/SdfFonts/AppHelper.hpp>
#include <GLES2/gl2.h>
#include <cassert>

namespace Fsl
{
  namespace
  {
    // A namespace for local configuration constants.
    // This helps to keep the code clean and organized.
    namespace LocalConfig
    {
      // The default Z position for the text.
      constexpr const float DefaultZPos = 0.0f;

      // File paths for the normal (bitmap) font resources.
      constexpr const IO::PathView NormalFontAtlasTexturePath("Bitmap.png");
      constexpr const IO::PathView NormalFontPath("Bitmap_SoftMaskFont.nbf");

      // File paths for the Signed Distance Field (SDF) font resources.
      constexpr const IO::PathView SdfFontAtlasTexturePath("Sdf.png");
      constexpr const IO::PathView SdfFontPath("Sdf_SdfFont.nbf");

      // File paths for the Multi-channel Signed Distance Field (MTSDF) font resources.
      constexpr const IO::PathView MtsdfFontAtlasTexturePath("Mtsdf.png");
      constexpr const IO::PathView MtsdfFontPath("Mtsdf_MtsdfFont.nbf");

      // File paths for the shader files.
      constexpr const IO::PathView TextVertShader("Text.vert");
      constexpr const IO::PathView TextFragShader("Text.frag");

      // File paths for the various SDF fragment shaders.
      constexpr const IO::PathView TextSdfFragShader("Text-sdf.frag");
      constexpr const IO::PathView TextSdfOutlineFragShader("Text-sdfOutline.frag");
      constexpr const IO::PathView TextSdfShadowFragShader("Text-sdfDropShadow.frag");
      constexpr const IO::PathView TextSdfShadowAndOutlineFragShader("Text-sdfDropShadowAndOutline.frag");
      constexpr const IO::PathView TextSdfContoursFragShader("Text-sdfContours.frag");

      // File paths for the various MTSDF fragment shaders.
      constexpr const IO::PathView TextMtsdfFragShader("Text-mtsdf.frag");
      constexpr const IO::PathView TextMtsdfOutlineFragShader("Text-mtsdfOutline.frag");
      constexpr const IO::PathView TextMtsdfShadowFragShader("Text-mtsdfDropShadow.frag");
      constexpr const IO::PathView TextMtsdfShadowAndOutlineFragShader("Text-mtsdfDropShadowAndOutline.frag");
      constexpr const IO::PathView TextMtsdfContoursFragShader("Text-mtsdfContours.frag");

      // The text to be rendered.
      constexpr const StringViewLite TextLine0("The quick brown fox jumps over the lazy dog! Hello World.");
      // constexpr StringViewLite TextLine1("abcdefghijklmnopqrstuvwxyz");
      // constexpr StringViewLite TextLine2("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
      // constexpr StringViewLite TextLine3("0123456789!\".:,;(){}");
    }

    // Helper function to read a texture from the content manager.
    GLES2::GLTexture ReadTexture(const IContentManager& contentManager, const IO::Path& path)
    {
      // Set texture parameters for linear filtering and clamp-to-edge wrapping.
      GLES2::GLTextureParameters params(GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
      // Read the bitmap from the content manager.
      auto bitmap = contentManager.ReadBitmap(path, PixelFormat::R8G8B8A8_UNORM);
      // Create and return a GLTexture.
      return {bitmap, params};
    }
  }


  // The constructor for the SdfFonts class.
  SdfFonts::SdfFonts(const DemoAppConfig& config)
    : DemoAppGLES2(config)
    , m_shared(config) // Initialize the shared UI and application logic.
    , m_nativeBatch(config.DemoServiceProvider.Get<IGraphicsService>()->GetNativeBatch2D()) // Get the native batch 2D service for drawing 2D graphics.
  {
    // Register the UI extension to handle DemoApp events.
    RegisterExtension(m_shared.GetUIDemoAppExtension());


    auto contentManager = GetContentManager();


    const PxSize1D line0YPx = PxSize1D::Create(0);
    const SpriteNativeAreaCalc& spriteNativeAreaCalc = m_shared.GetUIDemoAppExtension()->GetSpriteNativeAreaCalc();
    const uint32_t densityDpi = config.WindowMetrics.DensityDpi;

    // Generate the shader for normal text rendering.
    m_resources.ShaderNormal = GenerateShaderRecord(*contentManager, LocalConfig::TextVertShader, LocalConfig::TextFragShader);

    // Prepare the examples for normal, SDF, and MTSDF fonts.
    m_resources.Normal =
      PrepareExample(*contentManager, line0YPx, m_resources.ShaderNormal, LocalConfig::NormalFontPath, LocalConfig::NormalFontAtlasTexturePath,
                     LocalConfig::TextLine0, spriteNativeAreaCalc, densityDpi, m_positionsScratchpad);
    const auto line1YPx = m_resources.Normal.Font.Font.LineSpacingPx();
    m_resources.Sdf =
      PrepareExample(*contentManager, line1YPx, m_resources.ShaderNormal, LocalConfig::SdfFontPath, LocalConfig::SdfFontAtlasTexturePath,
                     LocalConfig::TextLine0, spriteNativeAreaCalc, densityDpi, m_positionsScratchpad);
    m_resources.Mtsdf =
      PrepareExample(*contentManager, line1YPx, m_resources.ShaderNormal, LocalConfig::MtsdfFontPath, LocalConfig::MtsdfFontAtlasTexturePath,
                     LocalConfig::TextLine0, spriteNativeAreaCalc, densityDpi, m_positionsScratchpad);

    // Generate the various SDF shaders.
    m_resources.ShadersSdf.Normal = GenerateShaderRecord(*contentManager, LocalConfig::TextVertShader, LocalConfig::TextSdfFragShader);
    m_resources.ShadersSdf.Outline = GenerateShaderRecord(*contentManager, LocalConfig::TextVertShader, LocalConfig::TextSdfOutlineFragShader);
    m_resources.ShadersSdf.Shadow = GenerateShaderRecord(*contentManager, LocalConfig::TextVertShader, LocalConfig::TextSdfShadowFragShader);
    m_resources.ShadersSdf.ShadowAndOutline =
      GenerateShaderRecord(*contentManager, LocalConfig::TextVertShader, LocalConfig::TextSdfShadowAndOutlineFragShader);
    m_resources.ShadersSdf.Contours = GenerateShaderRecord(*contentManager, LocalConfig::TextVertShader, LocalConfig::TextSdfContoursFragShader);

    // Generate the various MTSDF shaders.
    m_resources.ShadersMtsdf.Normal = GenerateShaderRecord(*contentManager, LocalConfig::TextVertShader, LocalConfig::TextMtsdfFragShader);
    m_resources.ShadersMtsdf.Outline = GenerateShaderRecord(*contentManager, LocalConfig::TextVertShader, LocalConfig::TextMtsdfOutlineFragShader);
    m_resources.ShadersMtsdf.Shadow = GenerateShaderRecord(*contentManager, LocalConfig::TextVertShader, LocalConfig::TextMtsdfShadowFragShader);
    m_resources.ShadersMtsdf.ShadowAndOutline =
      GenerateShaderRecord(*contentManager, LocalConfig::TextVertShader, LocalConfig::TextMtsdfShadowAndOutlineFragShader);
    m_resources.ShadersMtsdf.Contours = GenerateShaderRecord(*contentManager, LocalConfig::TextVertShader, LocalConfig::TextMtsdfContoursFragShader);

    // Get the fill texture from the shared resources.
    m_resources.FillTexture = m_shared.GetFillTexture();

    FSLLOG3_INFO("Ready");
  }


  SdfFonts::~SdfFonts() = default;

  // Handle key events.
  void SdfFonts::OnKeyEvent(const KeyEvent& event)
  {
    DemoAppGLES2::OnKeyEvent(event);

    m_shared.OnKeyEvent(event);
  }


  // Handle configuration changes, like window resizing.
  void SdfFonts::ConfigurationChanged(const DemoWindowMetrics& windowMetrics)
  {
    DemoAppGLES2::ConfigurationChanged(windowMetrics);

    m_shared.OnConfigurationChanged(windowMetrics);
  }


  // Update the application state.
  void SdfFonts::Update(const DemoTime& demoTime)
  {
    // Get the screen dimensions.
    const auto screenWidth = static_cast<float>(GetWindowSizePx().RawWidth());
    const auto screenHeight = static_cast<float>(GetWindowSizePx().RawHeight());
    const float screenOffsetX = screenWidth / 2.0f;
    const float screenOffsetY = screenHeight / 2.0f;

    // Create the projection matrix.
    m_resources.Projection = Matrix::CreateTranslation(-screenOffsetX, -screenOffsetY, 1.0f) * Matrix::CreateRotationX(MathHelper::ToRadians(180)) *
                             Matrix::CreateOrthographic(screenWidth, screenHeight, 1.0f, 10.0f);

    // Update the shared UI and application logic.
    m_shared.Update(demoTime);
  }


  // Draw the scene.
  void SdfFonts::Draw(const FrameInfo& frameInfo)
  {
    FSL_PARAM_NOT_USED(frameInfo);

    const PxSize2D currentSizePx = GetWindowSizePx();

    // Clear the screen.
    glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    auto contentOffset = m_shared.GetContentOffset();
    const auto fontDrawConfig = m_shared.GetFontDrawConfig();
    const auto fontScale = PxSize1DF::Create(fontDrawConfig.FontScale);
    const auto fontSdfMode = m_shared.GetSdfMode();
    const auto& fontSdfShader = SelectShaderRecord(fontSdfMode, fontDrawConfig.Type);

    auto& rSdfRecord = fontDrawConfig.Type == SdfType::Sdf ? m_resources.Sdf : m_resources.Mtsdf;

    // Calculate the position of each line of text.
    const PxPoint2 line0Px(contentOffset.X, contentOffset.Y);
    const PxPoint2 line1Px(contentOffset.X, line0Px.Y + m_resources.Normal.Font.Font.LineSpacingPx());
    const PxPoint2 line2Px(contentOffset.X, line1Px.Y + rSdfRecord.Font.Font.LineSpacingPx());
    const PxPoint2 line3Px(
      contentOffset.X, line2Px.Y + TypeConverter::UncheckedChangeTo<PxSize1D>(PxSize1DF(m_resources.Normal.Font.Font.LineSpacingPx()) * fontScale));

    const bool enableKerning = m_shared.GetKerningEnabled();
    const BitmapFontConfig fontConfigNormal(1.0f, enableKerning);
    const BitmapFontConfig fontConfigScaled(fontDrawConfig.FontScale, enableKerning);

    // Regenerate the text meshes if needed (e.g., if the text or font scale has changed).
    RegenerateMeshOnDemand(m_resources.Normal.Mesh, line0Px, m_resources.ShaderNormal, m_resources.Normal.Font, fontConfigNormal,
                           LocalConfig::TextLine0, m_positionsScratchpad);
    RegenerateMeshOnDemand(rSdfRecord.Mesh, line1Px, fontSdfShader, rSdfRecord.Font, fontConfigNormal, LocalConfig::TextLine0, m_positionsScratchpad);
    RegenerateMeshOnDemand(m_resources.Normal.ScaledMesh, line2Px, fontSdfShader, m_resources.Normal.Font, fontConfigScaled, LocalConfig::TextLine0,
                           m_positionsScratchpad);
    RegenerateMeshOnDemand(rSdfRecord.ScaledMesh, line3Px, fontSdfShader, rSdfRecord.Font, fontConfigScaled, LocalConfig::TextLine0,
                           m_positionsScratchpad);

    const auto baseLine0Px = line0Px + PxPoint2(PxValue(0), m_resources.Normal.Font.Font.BaseLinePx());
    const auto baseLine1Px = line1Px + PxPoint2(PxValue(0), rSdfRecord.Font.Font.BaseLinePx());
    const auto baseLine2Px =
      line2Px + PxPoint2(PxValue(0), TypeConverter::UncheckedChangeTo<PxSize1D>((PxSize1DF(m_resources.Normal.Font.Font.BaseLinePx()) * fontScale)));
    const auto baseLine3Px =
      line3Px + PxPoint2(PxValue(0), TypeConverter::UncheckedChangeTo<PxSize1D>((PxSize1DF(rSdfRecord.Font.Font.BaseLinePx()) * fontScale)));

    // Draw the baselines for debugging purposes.
    {
      constexpr auto BaseLineColor = Color(0xFF404040);

      m_nativeBatch->Begin();
      m_nativeBatch->DebugDrawLine(m_resources.FillTexture, baseLine0Px, PxPoint2(baseLine0Px.X + currentSizePx.Width(), baseLine0Px.Y),
                                   BaseLineColor);
      m_nativeBatch->DebugDrawLine(m_resources.FillTexture, baseLine1Px, PxPoint2(baseLine1Px.X + currentSizePx.Width(), baseLine1Px.Y),
                                   BaseLineColor);
      m_nativeBatch->DebugDrawLine(m_resources.FillTexture, baseLine2Px, PxPoint2(baseLine2Px.X + currentSizePx.Width(), baseLine2Px.Y),
                                   BaseLineColor);
      m_nativeBatch->DebugDrawLine(m_resources.FillTexture, baseLine3Px, PxPoint2(baseLine3Px.X + currentSizePx.Width(), baseLine3Px.Y),
                                   BaseLineColor);
      m_nativeBatch->End();
    }

    // Draw the text meshes.
    DrawMeshes(fontDrawConfig, fontSdfShader);

    // Draw the bounding boxes for debugging purposes.
    if (m_shared.GetBoundingBoxesEnabled())
    {
      m_nativeBatch->Begin(BlendState::Opaque);
      m_shared.DrawBoundingBoxes(*m_nativeBatch, line0Px, LocalConfig::TextLine0, m_resources.Normal.Font.Font, fontConfigNormal,
                                 m_positionsScratchpad);
      m_shared.DrawBoundingBoxes(*m_nativeBatch, line1Px, LocalConfig::TextLine0, rSdfRecord.Font.Font, fontConfigNormal, m_positionsScratchpad);
      m_shared.DrawBoundingBoxes(*m_nativeBatch, line2Px, LocalConfig::TextLine0, m_resources.Normal.Font.Font, fontConfigScaled,
                                 m_positionsScratchpad);
      m_shared.DrawBoundingBoxes(*m_nativeBatch, line3Px, LocalConfig::TextLine0, rSdfRecord.Font.Font, fontConfigScaled, m_positionsScratchpad);
      m_nativeBatch->End();
    }
    // Draw the UI.
    m_shared.Draw();
  }

  // Draw all the text meshes.
  void SdfFonts::DrawMeshes(const FontDrawConfig& fontDrawConfig, const SdfFonts::ShaderRecord& fontSdfShader)
  {
    glEnable(GL_CULL_FACE);

    glEnable(GL_BLEND);
    // Set the blend function for pre-multiplied alpha.
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    // glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);    // normal alpha
    // glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    FontDrawConfig drawConfig(fontDrawConfig);
    drawConfig.FontScale = 1.0f;

    {    // draw normal font
      const auto& example = m_resources.Normal;
      DrawTextMesh(example.Mesh, example.Font, m_resources.ShaderNormal, m_resources.Projection, drawConfig);
      DrawTextMesh(example.ScaledMesh, example.Font, m_resources.ShaderNormal, m_resources.Projection, fontDrawConfig);
    }

    // draw sdf/mtsdf font
    {
      // Set the blend function for normal alpha blending.
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

      const auto& example = fontDrawConfig.Type == SdfType::Sdf ? m_resources.Sdf : m_resources.Mtsdf;

      DrawTextMesh(example.Mesh, example.Font, fontSdfShader, m_resources.Projection, drawConfig);
      DrawTextMesh(example.ScaledMesh, example.Font, fontSdfShader, m_resources.Projection, fontDrawConfig);
    }

    // Unbind all the buffers and textures.
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
  }

  // Draw a single text mesh.
  void SdfFonts::DrawTextMesh(const MeshRecord& mesh, const FontRecord& fontRecord, const ShaderRecord& shader, const Matrix& projection,
                              const FontDrawConfig& fontDrawConfig)
  {
    // Set the shader program.
    glUseProgram(shader.Program.Get());

    // Set the active texture unit and bind the font texture.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontRecord.Texture.Get());

    // Load the matrices
    assert(shader.Location.ProjMatrix != GLES2::GLValues::InvalidLocation);
    assert(shader.Location.Texture != GLES2::GLValues::InvalidLocation);
    glUniformMatrix4fv(shader.Location.ProjMatrix, 1, 0, projection.DirectAccess());

    if (shader.Location.OutlineDistance != GLES2::GLValues::InvalidLocation)
    {
      const auto outlineDistance = (fontDrawConfig.OutlineDistance > 0.0f ? 0.5f * fontDrawConfig.OutlineDistance : 0.0f);
      glUniform1f(shader.Location.OutlineDistance, outlineDistance);
    }

    const float fontSdfSpread = std::max(fontRecord.Font.GetSdfParams().DistanceRange, 1.0f);
    if (shader.Location.Smoothing != GLES2::GLValues::InvalidLocation)
    {
      // The smoothing value is used for anti-aliasing the font edges.
      // It is calculated based on the SDF spread and the font scale.
      const auto smoothing = 0.25f / (fontSdfSpread * fontDrawConfig.FontScale);
      glUniform1f(shader.Location.Smoothing, smoothing);
    }
    if (shader.Location.ShadowOffset != GLES2::GLValues::InvalidLocation)
    {
      // The shader supports a shadow effect, so we calculate and pass the necessary offset.
      // The shadow offset required by the shader is in UV coordinates (0.0 to 1.0), not pixels.
      // We first need to determine the maximum possible offset based on the font's SDF properties.

      // 1. Convert the maximum SDF distance (`fontSdfSpread`, in pixels) to a UV coordinate offset.
      //    This is the largest distance we can shift the shadow before the SDF data becomes invalid.
      const auto maxOffsetX = fontSdfSpread / static_cast<float>(fontRecord.Texture.GetSize().RawWidth());
      const auto maxOffsetY = fontSdfSpread / static_cast<float>(fontRecord.Texture.GetSize().RawHeight());

      // 2. Calculate the final shadow offset based on the user's configuration.
      //    `fontDrawConfig.ShadowOffset` is a multiplier (e.g., from -1.0 to 1.0) for the direction and distance.
      const auto shadowOffsetX = fontDrawConfig.ShadowOffset.X != 0.0f ? maxOffsetX * fontDrawConfig.ShadowOffset.X : 0.0f;
      //    The Y-offset is negated because screen coordinates (Y-down) and texture UV coordinates (Y-up) are often inverted.
      const auto shadowOffsetY = fontDrawConfig.ShadowOffset.Y != 0.0f ? -(maxOffsetY * fontDrawConfig.ShadowOffset.Y) : 0.0f;

      // 3. Pass the final calculated 2D offset vector to the shader.
      glUniform2f(shader.Location.ShadowOffset, shadowOffsetX, shadowOffsetY);
    }
    if (shader.Location.ShadowSmoothing != GLES2::GLValues::InvalidLocation)
    {
      const auto shadowSmoothing = (fontDrawConfig.ShadowSmoothing > 0.0f ? 0.5f * fontDrawConfig.ShadowSmoothing : 0.0f);
      glUniform1f(shader.Location.ShadowSmoothing, shadowSmoothing);
    }
    if (shader.Location.ContourScale != GLES2::GLValues::InvalidLocation)
    {
      glUniform1f(shader.Location.ContourScale, fontDrawConfig.ContourScale);
    }

    glUniform1i(shader.Location.Texture, 0);


    // Bind the vertex and index buffers.
    glBindBuffer(mesh.VB.GetTarget(), mesh.VB.Get());
    glBindBuffer(mesh.IB.GetTarget(), mesh.IB.Get());

    // Enable the vertex attribute arrays.
    mesh.VB.EnableAttribArrays(mesh.AttribLink);

    // Draw the text.
    glDrawElements(GL_TRIANGLES, mesh.IB.GetGLCapacity(), mesh.IB.GetType(), nullptr);

    // Disable the vertex attribute arrays.
    mesh.VB.DisableAttribArrays(mesh.AttribLink);
  }


  // Prepare a font for rendering.
  SdfFonts::FontRecord SdfFonts::PrepareFont(const IContentManager& contentManager, const IO::Path& bitmapFontPath,
                                             const IO::Path& fontAtlasTexturePath, const SpriteNativeAreaCalc& spriteNativeAreaCalc,
                                             const uint32_t densityDpi)
  {
    // Read the font texture.
    auto texture = ReadTexture(contentManager, fontAtlasTexturePath);
    // Read the font data.
    auto font =
      AppHelper::ReadFont(spriteNativeAreaCalc, TypeConverter::To<PxExtent2D>(texture.GetSize()), contentManager, bitmapFontPath, densityDpi);

    return {std::move(texture), std::move(font)};
  }


  // Prepare an example for rendering.
  SdfFonts::ExampleRecord SdfFonts::PrepareExample(const IContentManager& contentManager, const PxSize1D lineYPx, const ShaderRecord& shader,
                                                   const IO::Path& bitmapFontPath, const IO::Path& fontAtlasTexturePath,
                                                   const StringViewLite& strView, const SpriteNativeAreaCalc& spriteNativeAreaCalc,
                                                   const uint32_t densityDpi, std::vector<SpriteFontGlyphPosition>& rPositionsScratchpad)
  {
    FSLLOG3_INFO("Preparing example");
    ExampleRecord result;

    FSLLOG3_INFO("- Loading font");
    // Prepare the font.
    result.Font = PrepareFont(contentManager, bitmapFontPath, fontAtlasTexturePath, spriteNativeAreaCalc, densityDpi);

    FSLLOG3_INFO("- Generating mesh");
    // Generate the initial meshes for the text.
    const BitmapFontConfig fontConfig(1.0f);
    result.Mesh = GenerateMesh(PxPoint2(PxValue(0), lineYPx), result.Font, fontConfig, strView, rPositionsScratchpad);
    result.ScaledMesh = GenerateMesh(PxPoint2(PxValue(0), lineYPx), result.Font, fontConfig, strView, rPositionsScratchpad);
    return result;
  }

  // Generate a shader record from vertex and fragment shader files.
  SdfFonts::ShaderRecord SdfFonts::GenerateShaderRecord(const IContentManager& contentManager, const IO::Path& vertShaderPath,
                                                        const IO::Path& fragShaderPath)
  {
    FSLLOG3_INFO("- Loading shaders '{}' & '{}'", vertShaderPath, fragShaderPath);
    ShaderRecord result;
    // Create the shader program from the shader files.
    result.Program.Reset(contentManager.ReadAllText(vertShaderPath), contentManager.ReadAllText(fragShaderPath));
    // Get the uniform locations.
    result.Location.OutlineDistance = result.Program.TryGetUniformLocation("g_outlineDistance");
    result.Location.ProjMatrix = result.Program.GetUniformLocation("g_matModelViewProj");
    result.Location.Smoothing = result.Program.TryGetUniformLocation("g_smoothing");
    result.Location.ShadowOffset = result.Program.TryGetUniformLocation("g_shadowOffset");
    result.Location.ShadowSmoothing = result.Program.TryGetUniformLocation("g_shadowSmoothing");
    result.Location.ContourScale = result.Program.TryGetUniformLocation("g_contourScale");
    result.Location.Texture = result.Program.GetUniformLocation("Texture");

    assert(result.Location.ProjMatrix != GLES2::GLValues::InvalidLocation);
    assert(result.Location.Texture != GLES2::GLValues::InvalidLocation);
    return result;
  }

  // Generate a mesh for a given string.
  SdfFonts::MeshRecord SdfFonts::GenerateMesh(const PxPoint2& dstPositionPx, const FontRecord& fontRecord, const BitmapFontConfig& fontConfig,
                                              const StringViewLite& strView, std::vector<SpriteFontGlyphPosition>& rPositionsScratchpad)
  {
    const TextureAtlasSpriteFont& font = fontRecord.Font;
    const PxSize2D fontTextureSize = fontRecord.Texture.GetSize();

    // Create vertex and index vectors.
    std::vector<VertexPositionTexture> vertices(strView.size() * 4);
    std::vector<uint16_t> indices(strView.size() * (3 * 2));

    if (strView.size() > rPositionsScratchpad.size())
    {
      rPositionsScratchpad.resize(strView.size());
    }

    // 1. Extract Render Rules:
    // For each character in the string, this finds the corresponding glyph information
    // (like texture coordinates, size, and offset) from the font data.
    auto scratchpadSpan = SpanUtil::AsSpan(rPositionsScratchpad);
    const bool gotRules = font.ExtractRenderRules(scratchpadSpan, strView);
    const auto positionsSpan = scratchpadSpan.subspan(0, gotRules ? strView.size() : 0);

    // 2. Generate Vertices:
    // This creates four vertices for each glyph, forming a quad.
    // Each vertex has a position and a texture coordinate that maps to the glyph in the font atlas.
    AppHelper::GenerateVertices(SpanUtil::AsSpan(vertices), dstPositionPx, positionsSpan, LocalConfig::DefaultZPos, fontTextureSize);

    // 3. Generate Indices:
    // This creates two triangles for each quad, which is how OpenGL renders quads.
    AppHelper::GenerateIndices(SpanUtil::AsSpan(indices), positionsSpan);

    // 4. Create Buffers:
    // The vertex and index data is uploaded to the GPU.
    GLES2::GLVertexBuffer vb(vertices, GL_STATIC_DRAW);
    // Attrib links are not generated here, but we expect RegenerateMeshOnDemand to handle it
    return {dstPositionPx, fontConfig, std::move(vertices), std::move(vb), GLES2::GLIndexBuffer(indices, GL_STATIC_DRAW), {}};
  }


  // Regenerate a mesh if its position, font config, or shader has changed.
  void SdfFonts::RegenerateMeshOnDemand(MeshRecord& rMeshRecord, const PxPoint2& dstPositionPx, const ShaderRecord& shader,
                                        const FontRecord& fontRecord, const BitmapFontConfig fontConfig, const StringViewLite& strView,
                                        std::vector<SpriteFontGlyphPosition>& rPositionsScratchpad)
  {
    bool shaderChanged = rMeshRecord.CachedShader != shader.Program.Get();
    // If nothing has changed, we don't need to do anything.
    if (rMeshRecord.Offset == dstPositionPx && rMeshRecord.FontConfig == fontConfig && !shaderChanged)
    {
      return;
    }
    // Cache the new state.
    rMeshRecord.Offset = dstPositionPx;
    rMeshRecord.FontConfig = fontConfig;
    rMeshRecord.CachedShader = shader.Program.Get();

    const TextureAtlasSpriteFont& font = fontRecord.Font;
    const PxSize2D fontTextureSize = fontRecord.Texture.GetSize();

    if (strView.size() > rPositionsScratchpad.size())
    {
      rPositionsScratchpad.resize(strView.size());
    }
    // Extract the render rules for the string.
    auto scratchpadSpan = SpanUtil::AsSpan(rPositionsScratchpad);
    const bool gotRules = font.ExtractRenderRules(scratchpadSpan, strView, fontConfig);
    const auto positionsSpan = scratchpadSpan.subspan(0, gotRules ? strView.size() : 0);

    // Generate the new vertices and update the vertex buffer.
    AppHelper::GenerateVertices(SpanUtil::AsSpan(rMeshRecord.Vertices), dstPositionPx, positionsSpan, LocalConfig::DefaultZPos, fontTextureSize);
    rMeshRecord.VB.SetData(0, rMeshRecord.Vertices.data(), rMeshRecord.Vertices.size());


    if (shaderChanged)
    {
      FSLLOG3_VERBOSE4("Updating attrib links");
      // Regenerate the attribute links to match the new shader.
      rMeshRecord.AttribLink = {GLES2::GLVertexAttribLink(shader.Program.GetAttribLocation("VertexPosition"),
                                                          rMeshRecord.VB.GetVertexElementIndex(VertexElementUsage::Position, 0)),
                                GLES2::GLVertexAttribLink(shader.Program.TryGetAttribLocation("VertexTextureCoord"),
                                                          rMeshRecord.VB.GetVertexElementIndex(VertexElementUsage::TextureCoordinate, 0))};
    }
  }

  // Select the appropriate shader based on the current SDF mode and type.
  const SdfFonts::ShaderRecord& SdfFonts::SelectShaderRecord(const SdfFontMode fontSdfMode, const SdfType fontSdfType)
  {
    const FontShaderRecord& shaders = fontSdfType == SdfType::Sdf ? m_resources.ShadersSdf : m_resources.ShadersMtsdf;

    switch (fontSdfMode)
    {
    case SdfFontMode::Outline:
      return shaders.Outline;
    case SdfFontMode::Shadow:
      return shaders.Shadow;
    case SdfFontMode::ShadowAndOutline:
      return shaders.ShadowAndOutline;
    case SdfFontMode::Contours:
      return shaders.Contours;
    default:
      return shaders.Normal;
    }
  }

}
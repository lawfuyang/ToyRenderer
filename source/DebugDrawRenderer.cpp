#include "Graphic.h"

#include "extern/debug_draw/debug_draw.hpp"

#include "CommonResources.h"
#include "Engine.h"
#include "GraphicPropertyGrid.h"
#include "RenderGraph.h"
#include "Scene.h"

extern RenderGraph::ResourceHandle g_DepthStencilBufferRDGTextureHandle;

enum EDebugDrawCategory { Point, Point_DepthTested, Line, Line_DepthTested, Glyph };

class DebugDrawRenderInterface : public dd::RenderInterface
{
public:
    // technically vertex formats for point, line & glyphs are different... but we don't care. just use a fat format for everything
    struct Vertex
    {
        Vector3 pos;   // 3D position
        Vector3 uv;    // Texture coordinates
        Vector3 color; // RGBA float
    };

    struct DebugDrawTexture : public dd::OpaqueTextureType
    {
        nvrhi::TextureHandle m_TextureHandle;
    };

    dd::GlyphTextureHandle createGlyphTexture(int width, int height, const void* pixels) override
    {
        // NOTE: need to init glyph texture here as the debugdraw will fre the pixels' memory immediately after this

        nvrhi::TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.format = nvrhi::Format::R8_UNORM;
        desc.debugName = "ImGui font texture";
        desc.initialState = nvrhi::ResourceStates::ShaderResource;
        m_GlyphTexture.m_TextureHandle = g_Graphic.m_NVRHIDevice->createTexture(desc);

        nvrhi::CommandListHandle commandList = g_Graphic.AllocateCommandList();
        SCOPED_COMMAND_LIST_AUTO_QUEUE(commandList, "DebugDraw Glyph Texture");

        commandList->writeTexture(m_GlyphTexture.m_TextureHandle, 0, 0, pixels, width);
        commandList->setPermanentTextureState(m_GlyphTexture.m_TextureHandle, nvrhi::ResourceStates::ShaderResource);
        commandList->commitBarriers();

        LOG_TO_CONSOLE("Initialized DebugDraw Glyph Texture");

        return &m_GlyphTexture;
    }

    void destroyGlyphTexture(dd::GlyphTextureHandle glyphTex) override
    {
        m_GlyphTexture.m_TextureHandle.Reset();
    }

    void drawPointList(const dd::DrawVertex* points, int count, bool depthEnabled) override
    {
        // Emulating points as billboarded quads, so each point will use 6 vertices
        // D3D doesn't support "point sprites" like OpenGL (gl_PointSize).
        const int maxVerts = DEBUG_DRAW_VERTEX_BUFFER_SIZE / 6;

        // OpenGL point size scaling produces gigantic points with the billboarding fallback.
        // This is some arbitrary down-scaling factor to more or less match the OpenGL samples.
        const float D3DPointSpriteScalingFactor = 0.005f;

        assert(points != nullptr);
        assert(count > 0 && count <= maxVerts);

        const View& view = g_Graphic.m_Scene->m_Views[Scene::Main];

        const int numVerts = count * 6;
        const int indexes[6] = { 0, 1, 2, 2, 3, 0 };

        std::vector<Vertex>& verts = m_Vertices[depthEnabled ? EDebugDrawCategory::Point_DepthTested : EDebugDrawCategory::Point];
        size_t v = verts.size();
        verts.resize(verts.size() + count * 6);

        // Expand each point into a quad:
        for (int p = 0; p < count; ++p)
        {
            const float ptSize = points[p].point.size * D3DPointSpriteScalingFactor;
            const Vector3 halfWidth = (ptSize * 0.5f) * view.m_Right; // X
            const Vector3 halfHeigh = (ptSize * 0.5f) * view.m_Up;    // Y
            const Vector3 origin = Vector3{ points[p].point.x, points[p].point.y, points[p].point.z };

            Vector3 corners[4];
            corners[0] = origin + halfWidth + halfHeigh;
            corners[1] = origin - halfWidth + halfHeigh;
            corners[2] = origin - halfWidth - halfHeigh;
            corners[3] = origin + halfWidth - halfHeigh;

            for (int i : indexes)
            {
                verts[v].pos.x = corners[i].x;
                verts[v].pos.y = corners[i].y;
                verts[v].pos.z = corners[i].z;

                verts[v].color.x = points[p].point.r;
                verts[v].color.y = points[p].point.g;
                verts[v].color.z = points[p].point.b;

                ++v;
            }
        }
    }

    void drawLineList(const dd::DrawVertex* lines, int count, bool depthEnabled) override
    {
        assert(lines != nullptr);
        assert(count > 0 && count <= DEBUG_DRAW_VERTEX_BUFFER_SIZE);

        // Copy into mapped buffer:
        std::vector<Vertex>& verts = m_Vertices[depthEnabled ? EDebugDrawCategory::Line_DepthTested : EDebugDrawCategory::Line];
        const size_t writeOffset = verts.size();
        verts.resize(verts.size() + count);
        for (int v = 0; v < count; ++v)
        {
            verts[writeOffset + v].pos.x = lines[v].line.x;
            verts[writeOffset + v].pos.y = lines[v].line.y;
            verts[writeOffset + v].pos.z = lines[v].line.z;

            verts[writeOffset + v].color.x = lines[v].line.r;
            verts[writeOffset + v].color.y = lines[v].line.g;
            verts[writeOffset + v].color.z = lines[v].line.b;
        }
    }

    void drawGlyphList(const dd::DrawVertex* glyphs, int count, dd::GlyphTextureHandle glyphTex) override
    {
        assert(glyphs != nullptr);
        assert(count > 0 && count <= DEBUG_DRAW_VERTEX_BUFFER_SIZE);

        // Copy into mapped buffer:
        std::vector<Vertex>& verts = m_Vertices[EDebugDrawCategory::Glyph];
        const size_t writeOffset = verts.size();
        verts.resize(verts.size() + count);
        for (int v = 0; v < count; ++v)
        {
            verts[writeOffset + v].pos.x = glyphs[v].glyph.x;
            verts[writeOffset + v].pos.y = glyphs[v].glyph.y;
            verts[writeOffset + v].pos.z = 0.0f;

            verts[writeOffset + v].uv.x = glyphs[v].glyph.u;
            verts[writeOffset + v].uv.y = glyphs[v].glyph.v;
            verts[writeOffset + v].uv.z = 0.0f;

            verts[writeOffset + v].color.x = glyphs[v].glyph.r;
            verts[writeOffset + v].color.y = glyphs[v].glyph.g;
            verts[writeOffset + v].color.z = glyphs[v].glyph.b;
        }
    }

    void drawLabel(ddVec3_In pos, const char* name)
    {
        View& mainView = g_Graphic.m_Scene->m_Views[Scene::EView::Main];

        // Only draw labels inside the camera frustum.
        if (mainView.m_Frustum.Contains(Vector3{ pos }))
        {
            const ddVec3 textColor = { 0.8f, 0.8f, 1.0f };
            dd::projectedText(name, pos, textColor, (float*)&mainView.m_ViewProjectionMatrix, 0, 0, g_Graphic.m_DisplayResolution.x, g_Graphic.m_DisplayResolution.y);
        }
    }

    void DrawTestObjects()
    {
        // Start a row of objects at this position:
        ddVec3 origin = { -15.0f, 0.0f, 0.0f };

        // Box with a point at it's center:
        drawLabel(origin, "box");
        dd::box(origin, dd::colors::Blue, 1.5f, 1.5f, 1.5f);
        dd::point(origin, dd::colors::White, 15.0f);
        origin[0] += 3.0f;

        // Sphere with a point at its center
        drawLabel(origin, "sphere");
        dd::sphere(origin, dd::colors::Red, 1.0f);
        dd::point(origin, dd::colors::White, 15.0f);
        origin[0] += 4.0f;

        // Two cones, one open and one closed:
        const ddVec3 condeDir = { 0.0f, 2.5f, 0.0f };
        origin[1] -= 1.0f;

        drawLabel(origin, "cone (open)");
        dd::cone(origin, condeDir, dd::colors::Yellow, 1.0f, 2.0f);
        dd::point(origin, dd::colors::White, 15.0f);
        origin[0] += 4.0f;

        drawLabel(origin, "cone (closed)");
        dd::cone(origin, condeDir, dd::colors::Cyan, 0.0f, 1.0f);
        dd::point(origin, dd::colors::White, 15.0f);
        origin[0] += 4.0f;

        // Axis-aligned bounding box:
        const ddVec3 bbMins = { -1.0f, -0.9f, -1.0f };
        const ddVec3 bbMaxs = { 1.0f,  2.2f,  1.0f };
        const ddVec3 bbCenter = {
            (bbMins[0] + bbMaxs[0]) * 0.5f,
            (bbMins[1] + bbMaxs[1]) * 0.5f,
            (bbMins[2] + bbMaxs[2]) * 0.5f
        };
        drawLabel(origin, "AABB");
        dd::aabb(bbMins, bbMaxs, dd::colors::Orange);
        dd::point(bbCenter, dd::colors::White, 15.0f);

        // Move along the Z for another row:
        origin[0] = -15.0f;
        origin[2] += 5.0f;

        // A big arrow pointing up:
        const ddVec3 arrowFrom = { origin[0], origin[1], origin[2] };
        const ddVec3 arrowTo = { origin[0], origin[1] + 5.0f, origin[2] };
        drawLabel(arrowFrom, "arrow");
        dd::arrow(arrowFrom, arrowTo, dd::colors::Magenta, 1.0f);
        dd::point(arrowFrom, dd::colors::White, 15.0f);
        dd::point(arrowTo, dd::colors::White, 15.0f);
        origin[0] += 4.0f;

        // Plane with normal vector:
        const ddVec3 planeNormal = { 0.0f, 1.0f, 0.0f };
        drawLabel(origin, "plane");
        dd::plane(origin, planeNormal, dd::colors::Yellow, dd::colors::Blue, 1.5f, 1.0f);
        dd::point(origin, dd::colors::White, 15.0f);
        origin[0] += 4.0f;

        // Circle on the Y plane:
        drawLabel(origin, "circle");
        dd::circle(origin, planeNormal, dd::colors::Orange, 1.5f, 15.0f);
        dd::point(origin, dd::colors::White, 15.0f);
        origin[0] += 3.2f;

        // Tangent basis vectors:
        const ddVec3 normal = { 0.0f, 1.0f, 0.0f };
        const ddVec3 tangent = { 1.0f, 0.0f, 0.0f };
        const ddVec3 bitangent = { 0.0f, 0.0f, 1.0f };
        origin[1] += 0.1f;
        drawLabel(origin, "tangent basis");
        dd::tangentBasis(origin, normal, tangent, bitangent, 2.5f);
        dd::point(origin, dd::colors::White, 15.0f);

        // And a set of intersecting axes:
        origin[0] += 4.0f;
        origin[1] += 1.0f;
        drawLabel(origin, "cross");
        dd::cross(origin, 2.0f);
        dd::point(origin, dd::colors::White, 15.0f);

        const ddVec3 color = { 0.8f, 0.3f, 1.0f };
        const ddVec3 frustumOrigin = { -8.0f, 0.5f, 14.0f };
        drawLabel(frustumOrigin, "frustum + axes");

        // The frustum will depict a fake camera:
        const Matrix proj = Matrix::CreatePerspectiveFieldOfView(ConvertToRadians(45.0f), 800.0f / 600.0f, 0.5f, 4.0f);
        const Matrix view = Matrix::CreateLookAt(Vector3(-8.0f, 0.5f, 14.0f), Vector3(-8.0f, 0.5f, -14.0f), Vector3::UnitY);
        const Matrix invClipMatrix = (view * proj).Invert();
        dd::frustum((float*)&invClipMatrix, color);

        // A white dot at the eye position:
        dd::point(frustumOrigin, dd::colors::White, 15.0f);

        // A set of arrows at the camera's origin/eye:
        const Matrix transform = Matrix::CreateRotationZ(ConvertToRadians(60.0f)) * Matrix::CreateTranslation(Vector3(-8.0f, 0.5f, 14.0f));
        dd::axisTriad((float*)&transform, 0.3f, 2.0f);

        // HUD text:
        const ddVec3 textColor = { 1.0f,  1.0f,  1.0f };
        const ddVec3 textPos2D = { 10.0f, 25.0f, 0.0f };
        dd::screenText("Screen Space Text Test", textPos2D, textColor);
    }

    // 5 Buffers: Points (DepthRead & DepthNone), Lines (DepthRead & DepthNone), Glyphs (DepthNone)
    std::vector<Vertex> m_Vertices[EnumUtils::Count<EDebugDrawCategory>()];

    DebugDrawTexture m_GlyphTexture;
};

class DebugDrawRenderer : public IRenderer
{
public:
    DebugDrawRenderInterface m_DebugDrawRenderInterface;

    // 5 Buffers: Points (DepthRead & DepthNone), Lines (DepthRead & DepthNone), Glyphs (DepthNone)
    nvrhi::BufferHandle m_VertexBuffer[5];

    DebugDrawRenderer()
        : IRenderer("DebugDrawRenderer")
    {}

    ~DebugDrawRenderer()
    {
        dd::shutdown();
    }

    void Initialize() override
    {
        dd::initialize(&m_DebugDrawRenderInterface);
    }

    bool Setup(RenderGraph& renderGraph) override
	{
        const GraphicPropertyGrid::DebugControllables& debugControllables = g_GraphicPropertyGrid.m_DebugControllables;

        // clear debug draw vertices' data if option to render debug primitives is disabled
        if (!debugControllables.m_bRenderDebugDraw)
        {
            return false;
        }

		// always add read dependency on depth buffer as the depth tested debug draw primitives will only be processed in the Render function
        renderGraph.AddReadDependency(g_DepthStencilBufferRDGTextureHandle);

        return true;
	}

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        const GraphicPropertyGrid::DebugControllables& debugControllables = g_GraphicPropertyGrid.m_DebugControllables;

        if (debugControllables.m_bRenderDebugDrawDemo)
        {
            PROFILE_SCOPED("Draw Test DebugDraw Primitives");

            m_DebugDrawRenderInterface.DrawTestObjects();
        }

        if (debugControllables.m_bRenderGrid)
        {
            // Grid from -50 to +50 in both X & Z
            dd::xzSquareGrid(-50.0f, 50.0f, 0.0f, 1.0f, dd::colors::Green);
        }

        Scene* scene = g_Graphic.m_Scene.get();
        if (debugControllables.m_bRenderSceneBS)
        {
            const Sphere& sceneBS = scene->m_BoundingSphere;
            dd::sphere((float*)&sceneBS.Center, dd::colors::White, sceneBS.Radius);
        }
        if (debugControllables.m_bRenderSceneAABB)
        {
            const AABB& sceneAABB = scene->m_AABB;
            dd::box((float*)&sceneAABB.Center, dd::colors::White, sceneAABB.Extents.x * 2, sceneAABB.Extents.y * 2, sceneAABB.Extents.z * 2, 0);
        }

        if (!dd::hasPendingDraws())
        {
            return;
        }

        {
            PROFILE_SCOPED("Flush & Retrieve DebugDraw verts");

            dd::flush();
        }

        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        nvrhi::FramebufferDesc frameBufferDesc;
        frameBufferDesc.addColorAttachment(g_Graphic.GetCurrentBackBuffer());

        nvrhi::TextureHandle depthBuffer;

        const bool bNeedsDepthTesting = !m_DebugDrawRenderInterface.m_Vertices[EDebugDrawCategory::Point_DepthTested].empty() || !m_DebugDrawRenderInterface.m_Vertices[EDebugDrawCategory::Line_DepthTested].empty();
        if (bNeedsDepthTesting)
        {
            depthBuffer = renderGraph.GetTexture(g_DepthStencilBufferRDGTextureHandle);
            frameBufferDesc.setDepthAttachment(depthBuffer);
            frameBufferDesc.depthAttachment.isReadOnly = true;
        }

        nvrhi::FramebufferHandle frameBuffer = device->createFramebuffer(frameBufferDesc);

        // constant buffer
        struct ConstantBufferData
        {
            Matrix m_ViewProjMatrix;
            Vector2 m_ScreenDimension;
        } constantBufferData{ scene->m_Views[Scene::EView::Main].m_ViewProjectionMatrix, { (float)g_Graphic.m_DisplayResolution.x, (float)g_Graphic.m_DisplayResolution.y } };

        // shader resources
        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::PushConstants(0, sizeof(constantBufferData)),
            nvrhi::BindingSetItem::Texture_SRV(0, m_DebugDrawRenderInterface.m_GlyphTexture.m_TextureHandle),
            nvrhi::BindingSetItem::Sampler(0, g_CommonResources.LinearClampSampler)
        };
        nvrhi::BindingSetHandle bindingSet;
        nvrhi::BindingLayoutHandle bindingLayout;
        g_Graphic.CreateBindingSetAndLayout(bindingSetDesc, bindingSet, bindingLayout);

        // PSO
        nvrhi::GraphicsPipelineDesc PSODesc;
        PSODesc.inputLayout = g_CommonResources.DebugDrawLayout;
        PSODesc.bindingLayouts = { bindingLayout };

        nvrhi::GraphicsState drawState;
        drawState.viewport.addViewportAndScissorRect(nvrhi::Viewport{ (float)g_Graphic.m_DisplayResolution.x, (float)g_Graphic.m_DisplayResolution.y });
        drawState.framebuffer = frameBuffer;
        drawState.bindings = { bindingSet };

        auto DrawDebugVerticesHelper = [&](EDebugDrawCategory category, nvrhi::PrimitiveType primType, const nvrhi::RenderState& renderState, nvrhi::ShaderHandle VS, nvrhi::ShaderHandle PS)
            {
                const std::vector<DebugDrawRenderInterface::Vertex>& vertices = m_DebugDrawRenderInterface.m_Vertices[category];
                if (vertices.empty())
                {
                    return;
                }

                PROFILE_SCOPED("Draw Debug Vertices");

                const size_t nbBytes = sizeof(DebugDrawRenderInterface::Vertex) * vertices.size();

                if (!m_VertexBuffer[category] || m_VertexBuffer[category]->getDesc().byteSize < nbBytes)
                {
                    nvrhi::BufferDesc desc;
                    desc.byteSize = nbBytes;
                    desc.debugName = "DebugDraw vertex buffer";
                    desc.isVertexBuffer = true;

                    m_VertexBuffer[category] = device->createBuffer(desc);

                    LOG_TO_CONSOLE("DebugDraw vertex buffer [%s]: [%f] MB", EnumUtils::ToString(category), BYTES_TO_MB(nbBytes));
                }

                commandList->writeBuffer(m_VertexBuffer[category], vertices.data(), nbBytes);

                PSODesc.primType = primType;
                PSODesc.renderState = renderState;
                PSODesc.VS = VS;
                PSODesc.PS = PS;

                drawState.pipeline = g_Graphic.GetOrCreatePSO(PSODesc, frameBuffer);
                drawState.vertexBuffers = { nvrhi::VertexBufferBinding{ m_VertexBuffer[category], 0, 0} };

                commandList->setGraphicsState(drawState);

                commandList->setPushConstants(&constantBufferData, sizeof(constantBufferData));

                nvrhi::DrawArguments drawArguments;
                drawArguments.vertexCount = (uint32_t)vertices.size();

                commandList->draw(drawArguments);
            };

        // Point
        DrawDebugVerticesHelper(
            EDebugDrawCategory::Point,
            nvrhi::PrimitiveType::TriangleList,
            nvrhi::RenderState{ nvrhi::BlendState{ g_CommonResources.BlendOpaque }, g_CommonResources.DepthNoneStencilNone, g_CommonResources.CullNone },
            g_Graphic.GetShader("debugdraw_VS_LinePoint"),
            g_Graphic.GetShader("debugdraw_PS_LinePoint"));

        // Point_DepthTested
        DrawDebugVerticesHelper(
            EDebugDrawCategory::Point_DepthTested,
            nvrhi::PrimitiveType::TriangleList,
            nvrhi::RenderState{ nvrhi::BlendState{ g_CommonResources.BlendOpaque }, g_CommonResources.DepthReadStencilNone, g_CommonResources.CullNone },
            g_Graphic.GetShader("debugdraw_VS_LinePoint"),
            g_Graphic.GetShader("debugdraw_PS_LinePoint"));

        // Line
        DrawDebugVerticesHelper(
            EDebugDrawCategory::Line,
            nvrhi::PrimitiveType::LineList,
            nvrhi::RenderState{ nvrhi::BlendState{ g_CommonResources.BlendOpaque }, g_CommonResources.DepthNoneStencilNone, g_CommonResources.CullNone },
            g_Graphic.GetShader("debugdraw_VS_LinePoint"),
            g_Graphic.GetShader("debugdraw_PS_LinePoint"));

        // Line_DepthTested
        DrawDebugVerticesHelper(
            EDebugDrawCategory::Line_DepthTested,
            nvrhi::PrimitiveType::LineList,
            nvrhi::RenderState{ nvrhi::BlendState{ g_CommonResources.BlendOpaque }, g_CommonResources.DepthReadStencilNone, g_CommonResources.CullNone },
            g_Graphic.GetShader("debugdraw_VS_LinePoint"),
            g_Graphic.GetShader("debugdraw_PS_LinePoint"));

        // Glyph
        DrawDebugVerticesHelper(
            EDebugDrawCategory::Glyph,
            nvrhi::PrimitiveType::TriangleList,
            nvrhi::RenderState{ nvrhi::BlendState{ g_CommonResources.BlendDebugDraw }, g_CommonResources.DepthNoneStencilNone, g_CommonResources.CullNone },
            g_Graphic.GetShader("debugdraw_VS_TextGlyph"),
            g_Graphic.GetShader("debugdraw_PS_TextGlyph"));

        for (std::vector<DebugDrawRenderInterface::Vertex>& elem : m_DebugDrawRenderInterface.m_Vertices)
        {
            elem.clear();
        }
    }
};

static DebugDrawRenderer gs_DebugDrawRenderer;
IRenderer* g_DebugDrawRenderer = &gs_DebugDrawRenderer;

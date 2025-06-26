#include "Graphic.h"

#include "extern/imgui/imgui.h"

#include "Engine.h"
#include "CommonResources.h"

class IMGUIRenderer : public IRenderer
{
    struct IMGUIPassParameters
    {
        Matrix m_ProjMatrix;
    };

    nvrhi::InputLayoutHandle m_InputLayout;

    std::vector<ImDrawVert> m_Vertices;
    std::vector<ImDrawIdx> m_Indices;
    std::vector<nvrhi::TextureHandle> m_Textures;

    nvrhi::BufferHandle m_VertexBuffer;
    nvrhi::BufferHandle m_IndexBuffer;

public:
    IMGUIRenderer() : IRenderer{ "IMGUIRenderer" } {}

    void Initialize() override
    {
        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        ImGuiIO& io = ImGui::GetIO();

		static const nvrhi::VertexAttributeDesc kLayout[] = {
	        { "POSITION", nvrhi::Format::RG32_FLOAT,  1, 0, offsetof(ImDrawVert,pos), sizeof(ImDrawVert), false },
	        { "TEXCOORD", nvrhi::Format::RG32_FLOAT,  1, 0, offsetof(ImDrawVert,uv),  sizeof(ImDrawVert), false },
	        { "COLOR",    nvrhi::Format::RGBA8_UNORM, 1, 0, offsetof(ImDrawVert,col), sizeof(ImDrawVert), false },
		};
		m_InputLayout = device->createInputLayout(kLayout, (uint32_t)std::size(kLayout), nullptr);
    }

    void UpdateTexture(nvrhi::CommandListHandle commandList, ImTextureData* tex)
    {
        if (tex->Status == ImTextureStatus_OK)
        {
            return;
        }

        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        switch (tex->Status)
        {
        case ImTextureStatus::ImTextureStatus_WantCreate:
        {
            assert(tex->TexID == 0 && tex->BackendUserData == nullptr);
            assert(tex->Format == ImTextureFormat_RGBA32);

            LOG_DEBUG("Create IMGUI Texture: %dx%d", tex->Width, tex->Height);

            const uint32_t textureIdx = m_Textures.size();

            nvrhi::TextureDesc fontTextureDesc;
            fontTextureDesc.width = tex->Width;
            fontTextureDesc.height = tex->Height;
            fontTextureDesc.format = nvrhi::Format::RGBA8_UNORM;
            fontTextureDesc.debugName = StringFormat("ImGui texture : %d", textureIdx);
            fontTextureDesc.initialState = nvrhi::ResourceStates::ShaderResource;

            // just maintain an ever-growing list of textures. dont care about re-using them
            nvrhi::TextureHandle& newTexture = m_Textures.emplace_back();
            newTexture = device->createTexture(fontTextureDesc);

            commandList->writeTexture(newTexture, 0, 0, tex->GetPixels(), tex->GetPitch());

            tex->SetTexID(textureIdx);

            break;
        }
        case ImTextureStatus::ImTextureStatus_WantUpdates:
        {
            nvrhi::TextureHandle textureHandle = m_Textures.at(tex->TexID);

            LOG_DEBUG("Update IMGUI Texture %d: [x:%d, y:%d, w:%d, h:%d]", tex->GetTexID(), tex->UpdateRect.x, tex->UpdateRect.y, tex->UpdateRect.w, tex->UpdateRect.h);

            nvrhi::TextureDesc textureDesc;
            textureDesc.width = tex->UpdateRect.w;
            textureDesc.height = tex->UpdateRect.h;
            textureDesc.format = textureHandle->getDesc().format;

            nvrhi::StagingTextureHandle stagingTexture = device->createStagingTexture(textureDesc, nvrhi::CpuAccessMode::Write);
            assert(stagingTexture);

            size_t rowPitch;
            void* mappedPtr = device->mapStagingTexture(stagingTexture, nvrhi::TextureSlice{}, nvrhi::CpuAccessMode::Write, &rowPitch);

            for (uint32_t y = 0; y < tex->UpdateRect.h; y++)
            {
                memcpy((void*)((uintptr_t)mappedPtr + y * rowPitch), tex->GetPixelsAt(tex->UpdateRect.x, tex->UpdateRect.y + y), rowPitch);
            }

            device->unmapStagingTexture(stagingTexture);

            commandList->copyTexture(
                textureHandle,
                nvrhi::TextureSlice{ tex->UpdateRect.x, tex->UpdateRect.y, 0, tex->UpdateRect.w, tex->UpdateRect.h, 1 },
                stagingTexture,
                nvrhi::TextureSlice{});

            break;
        }
        case ImTextureStatus::ImTextureStatus_WantDestroy:
        {
            LOG_DEBUG("Destroy IMGUI Texture %d", tex->GetTexID());
            assert(0); // TODO
            break;
        }
        };

        tex->SetStatus(tex->Status == ImTextureStatus_WantDestroy ? ImTextureStatus_Destroyed : ImTextureStatus_OK);
    }

    void UploadVertexAndIndexBuffers(nvrhi::CommandListHandle commandList, ImDrawData* drawData)
    {
        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        // VB
        {
            const uint32_t requiredSize = drawData->TotalVtxCount * sizeof(ImDrawVert);
            const uint32_t reallocateSize = (drawData->TotalVtxCount + 5000) * sizeof(ImDrawVert);
            if (!m_VertexBuffer || (m_VertexBuffer->getDesc().byteSize < requiredSize))
            {
                PROFILE_SCOPED("Create Vertex Buffer");

                nvrhi::BufferDesc desc;
                desc.byteSize = reallocateSize;
                desc.debugName = "ImGui vertex buffer";
                desc.isVertexBuffer = true;

                m_VertexBuffer = device->createBuffer(desc);
            }
        }

        // IB
        {
            const uint32_t requiredSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);
            const uint32_t reallocateSize = (drawData->TotalIdxCount + 5000) * sizeof(ImDrawIdx);

            if (!m_IndexBuffer || (m_IndexBuffer->getDesc().byteSize < requiredSize))
            {
                PROFILE_SCOPED("Create Index Buffer");

                nvrhi::BufferDesc desc;
                desc.byteSize = reallocateSize;
                desc.debugName = "ImGui index buffer";
                desc.isIndexBuffer = true;

                m_IndexBuffer = device->createBuffer(desc);
            }
        }

        // prep data into linear buffers for upload
        m_Vertices.clear();
        m_Indices.clear();

        for (ImDrawList* drawList : drawData->CmdLists)
        {
            m_Vertices.insert(m_Vertices.end(), drawList->VtxBuffer.begin(), drawList->VtxBuffer.end());
            m_Indices.insert(m_Indices.end(), drawList->IdxBuffer.begin(), drawList->IdxBuffer.end());
        }

        PROFILE_SCOPED("Write Buffers");

        commandList->writeBuffer(m_VertexBuffer, m_Vertices.data(), m_Vertices.size() * sizeof(ImDrawVert));
        commandList->writeBuffer(m_IndexBuffer, m_Indices.data(), m_Indices.size() * sizeof(ImDrawIdx));
    }

    void Render(nvrhi::CommandListHandle commandList, const RenderGraph& renderGraph) override
    {
        {
            PROFILE_SCOPED("ImGui::Render");
            ImGui::Render();
        }

        ImDrawData* drawData = ImGui::GetDrawData();

        if (drawData->CmdListsCount == 0)
        {
            return;
        }

        nvrhi::DeviceHandle device = g_Graphic.m_NVRHIDevice;

        if (drawData->Textures)
        {
            for (ImTextureData* tex : *drawData->Textures)
            {
                UpdateTexture(commandList, tex);
            }
        }

        // re-alloc & upload imgui vtx/idx data if needed
        UploadVertexAndIndexBuffers(commandList, drawData);

        // Render Targets & Depth Buffer
        nvrhi::FramebufferDesc frameBufferDesc;
        frameBufferDesc.addColorAttachment(g_Graphic.GetCurrentBackBuffer());
        nvrhi::FramebufferHandle frameBuffer = device->createFramebuffer(frameBufferDesc);

        // graphics state
        const ImGuiIO& io = ImGui::GetIO();
        nvrhi::GraphicsState drawState;
        drawState.framebuffer = frameBuffer;
        drawState.viewport.viewports.push_back(nvrhi::Viewport{ io.DisplaySize.x * io.DisplayFramebufferScale.x, io.DisplaySize.y * io.DisplayFramebufferScale.y });
        drawState.viewport.scissorRects.resize(1);  // updated below

        // Setup orthographic projection matrix into our constant buffer
        // Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right).
        IMGUIPassParameters passParameters;

        const float L = drawData->DisplayPos.x;
        const float R = drawData->DisplayPos.x + drawData->DisplaySize.x;
        const float T = drawData->DisplayPos.y;
        const float B = drawData->DisplayPos.y + drawData->DisplaySize.y;

        Matrix projMatrix
        {
            2.0f / (R - L)   , 0.0f,              0.0f, 0.0f,
            0.0f             , 2.0f / (T - B),    0.0f, 0.0f,
            0.0f             , 0.0f,              0.5f, 0.0f,
            (R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f
        };
        passParameters.m_ProjMatrix = projMatrix;

        // vertex & index buffers
        static_assert(sizeof(ImDrawIdx) == sizeof(uint16_t));
        drawState.vertexBuffers.push_back(nvrhi::VertexBufferBinding{ m_VertexBuffer, 0, 0 });
        drawState.indexBuffer.buffer = m_IndexBuffer;
        drawState.indexBuffer.format = nvrhi::Format::R16_UINT;
        drawState.indexBuffer.offset = 0;

        assert(nvrhi::getFormatInfo(drawState.indexBuffer.format).bytesPerBlock == sizeof(ImDrawIdx));

        const nvrhi::BlendState::RenderTarget kRTBlendState =
        {
            true,                             // blendEnable
            nvrhi::BlendFactor::SrcAlpha,     // srcBlend
            nvrhi::BlendFactor::InvSrcAlpha,  // destBlend
            nvrhi::BlendOp::Add,              // blendOp
            nvrhi::BlendFactor::InvSrcAlpha,  // srcBlendAlpha
            nvrhi::BlendFactor::Zero,         // destBlendAlpha
            nvrhi::BlendOp::Add,              // blendOpAlpha
            nvrhi::ColorMask::All             // colorWriteMask
        };

        nvrhi::BlendState RTBlendState;
        RTBlendState.targets[0] = kRTBlendState;

        // PSO
        nvrhi::GraphicsPipelineDesc PSODesc;
        PSODesc.inputLayout = m_InputLayout;
        PSODesc.VS = g_Graphic.GetShader("imgui_VS_Main");
        PSODesc.PS = g_Graphic.GetShader("imgui_PS_Main");
        PSODesc.renderState = nvrhi::RenderState{ RTBlendState, g_CommonResources.DepthNoneStencilNone, g_CommonResources.CullNone };

        // Render command lists
        // (Because we merged all buffers into a single one, we maintain our own offset into them)
        uint32_t global_vtx_offset = 0;
        uint32_t global_idx_offset = 0;
        for (const ImDrawList* drawList : drawData->CmdLists)
        {
            for (const ImDrawCmd& cmd : drawList->CmdBuffer)
            {
                // Apply Scissor, Bind texture, Draw
                nvrhi::Rect& r = drawState.viewport.scissorRects[0];
                r.minX = (int)cmd.ClipRect.x;
                r.maxY = (int)cmd.ClipRect.y;
                r.maxX = (int)cmd.ClipRect.z;
                r.minY = (int)cmd.ClipRect.w;

                // shader resources
                nvrhi::BindingSetDesc bindingSetDesc;
                bindingSetDesc.bindings = {
                    nvrhi::BindingSetItem::PushConstants(0, sizeof(passParameters)),
                    nvrhi::BindingSetItem::Texture_SRV(0, m_Textures.at(cmd.TexRef.GetTexID())),
                    nvrhi::BindingSetItem::Sampler(0, g_CommonResources.LinearWrapSampler)
                };
                nvrhi::BindingSetHandle bindingSet;
                nvrhi::BindingLayoutHandle bindingLayout;
                g_Graphic.CreateBindingSetAndLayout(bindingSetDesc, bindingSet, bindingLayout);

                PSODesc.bindingLayouts = { bindingLayout };
                drawState.pipeline = g_Graphic.GetOrCreatePSO(PSODesc, frameBuffer);
                drawState.bindings = { bindingSet };

                commandList->setGraphicsState(drawState);

                commandList->setPushConstants(&passParameters, sizeof(passParameters));

                nvrhi::DrawArguments drawArguments;
                drawArguments.vertexCount = cmd.ElemCount;
                drawArguments.startIndexLocation = cmd.IdxOffset + global_idx_offset;
                drawArguments.startVertexLocation = cmd.VtxOffset + global_vtx_offset;
                commandList->drawIndexed(drawArguments);

            }
            global_idx_offset += (uint32_t)drawList->IdxBuffer.size();
            global_vtx_offset += (uint32_t)drawList->VtxBuffer.size();
        }
    }
};

static IMGUIRenderer gs_IMGUIRenderer;
IRenderer* g_IMGUIRenderer = &gs_IMGUIRenderer;

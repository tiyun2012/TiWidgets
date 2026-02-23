#include "dx12_canvas.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {
constexpr float kPi = 3.14159265358979323846f;
const char* kVS = R"(
cbuffer View : register(b0)
{
    float2 uScreenSize;
};

struct VS_INPUT {
    float2 pos : POSITION;
    float4 col : COLOR;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 col : COLOR;
};

PS_INPUT main(VS_INPUT input) {
    PS_INPUT output;
    float2 ndc = float2(
        (input.pos.x / uScreenSize.x) * 2.0f - 1.0f,
        1.0f - (input.pos.y / uScreenSize.y) * 2.0f);
    output.pos = float4(ndc, 0.0f, 1.0f);
    output.col = input.col;
    return output;
}
)";

const char* kPS = R"(
struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 col : COLOR;
};

float4 main(PS_INPUT input) : SV_Target {
    return input.col;
}
)";
}

DX12Canvas::DX12Canvas(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, float targetWidth, float targetHeight)
    : device_(device), commandList_(commandList), targetWidth_(targetWidth), targetHeight_(targetHeight)
{
    initializePipeline();
    createVertexBuffer(MAX_VERTICES);
}

void DX12Canvas::initializePipeline()
{
    ComPtr<ID3DBlob> vs, ps, errors;
    HRESULT hr = D3DCompile(kVS, strlen(kVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs, &errors);
    if (FAILED(hr)) {
        if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());
        throw std::runtime_error("Failed to compile vertex shader");
    }
    hr = D3DCompile(kPS, strlen(kPS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &ps, &errors);
    if (FAILED(hr)) {
        if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());
        throw std::runtime_error("Failed to compile pixel shader");
    }

    // Root signature: one CBV (b0) and IA only.
    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param.Constants.ShaderRegister = 0;    // b0
    param.Constants.RegisterSpace = 0;
    param.Constants.Num32BitValues = 2;    // float2 screen size
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters = &param;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig;
    hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &errors);
    if (FAILED(hr)) throw std::runtime_error("Failed to serialize root signature");
    hr = device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
    if (FAILED(hr)) throw std::runtime_error("Failed to create root signature");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_RASTERIZER_DESC rast{};
    rast.FillMode = D3D12_FILL_MODE_SOLID;
    rast.CullMode = D3D12_CULL_MODE_NONE;
    rast.FrontCounterClockwise = FALSE;
    rast.DepthClipEnable = TRUE;

    D3D12_BLEND_DESC blend{};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    auto& rt = blend.RenderTarget[0];
    rt.BlendEnable = FALSE;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.StencilEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.InputLayout = { layout, _countof(layout) };
    pso.pRootSignature = rootSignature_.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.RasterizerState = rast;
    pso.BlendState = blend;
    pso.DepthStencilState = depth;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;

    hr = device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipelineState_));
    if (FAILED(hr)) throw std::runtime_error("Failed to create pipeline state");
}

void DX12Canvas::createVertexBuffer(size_t vertexCount)
{
    const UINT bufferSize = static_cast<UINT>(vertexCount * sizeof(D3DVertex));

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bufferSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device_->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexBuffer_));
    if (FAILED(hr)) throw std::runtime_error("Failed to create vertex buffer");

    vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexBufferView_.StrideInBytes = sizeof(D3DVertex);
    vertexBufferView_.SizeInBytes = bufferSize;
}

void DX12Canvas::drawRectangle(const DFRect& rect, const DFColor& color)
{
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    if (vertices_.size() + 6 > MAX_VERTICES) flush();
    auto makeV = [&](float x, float y) {
        return D3DVertex{{x, y}, {color.r, color.g, color.b, color.a}};
    };
    D3DVertex v1 = makeV(rect.x, rect.y);
    D3DVertex v2 = makeV(rect.x + rect.width, rect.y);
    D3DVertex v3 = makeV(rect.x, rect.y + rect.height);
    D3DVertex v4 = makeV(rect.x + rect.width, rect.y + rect.height);
    vertices_.push_back(v1); vertices_.push_back(v2); vertices_.push_back(v3);
    vertices_.push_back(v2); vertices_.push_back(v4); vertices_.push_back(v3);
}

void DX12Canvas::drawRoundedRectangle(const DFRect& rect, float radius, const DFColor& color)
{
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }

    const float maxRadius = std::min(rect.width, rect.height) * 0.5f;
    const float r = std::clamp(radius, 0.0f, maxRadius);
    if (r <= 0.01f) {
        drawRectangle(rect, color);
        return;
    }

    auto makeVertex = [&](float x, float y) {
        return D3DVertex{{x, y}, {color.r, color.g, color.b, color.a}};
    };
    auto addTriangle = [&](const D3DVertex& a, const D3DVertex& b, const D3DVertex& c) {
        if (vertices_.size() + 3 > MAX_VERTICES) {
            flush();
        }
        vertices_.push_back(a);
        vertices_.push_back(b);
        vertices_.push_back(c);
    };
    auto addRect = [&](float x, float y, float w, float h) {
        if (w <= 0.0f || h <= 0.0f) {
            return;
        }
        D3DVertex v1 = makeVertex(x, y);
        D3DVertex v2 = makeVertex(x + w, y);
        D3DVertex v3 = makeVertex(x, y + h);
        D3DVertex v4 = makeVertex(x + w, y + h);
        addTriangle(v1, v2, v3);
        addTriangle(v2, v4, v3);
    };

    addRect(rect.x + r, rect.y + r, rect.width - r * 2.0f, rect.height - r * 2.0f);
    addRect(rect.x + r, rect.y, rect.width - r * 2.0f, r);
    addRect(rect.x + r, rect.y + rect.height - r, rect.width - r * 2.0f, r);
    addRect(rect.x, rect.y + r, r, rect.height - r * 2.0f);
    addRect(rect.x + rect.width - r, rect.y + r, r, rect.height - r * 2.0f);

    const int segments = std::max(6, static_cast<int>(std::ceil(r * 0.75f)));
    auto addCornerFan = [&](float cx, float cy, float startAngle, float endAngle) {
        const D3DVertex center = makeVertex(cx, cy);
        for (int i = 0; i < segments; ++i) {
            const float t0 = static_cast<float>(i) / static_cast<float>(segments);
            const float t1 = static_cast<float>(i + 1) / static_cast<float>(segments);
            const float a0 = startAngle + (endAngle - startAngle) * t0;
            const float a1 = startAngle + (endAngle - startAngle) * t1;
            const D3DVertex p0 = makeVertex(cx + std::cos(a0) * r, cy + std::sin(a0) * r);
            const D3DVertex p1 = makeVertex(cx + std::cos(a1) * r, cy + std::sin(a1) * r);
            addTriangle(center, p0, p1);
        }
    };

    addCornerFan(rect.x + r, rect.y + r, kPi, kPi * 1.5f);
    addCornerFan(rect.x + rect.width - r, rect.y + r, kPi * 1.5f, kPi * 2.0f);
    addCornerFan(rect.x + rect.width - r, rect.y + rect.height - r, 0.0f, kPi * 0.5f);
    addCornerFan(rect.x + r, rect.y + rect.height - r, kPi * 0.5f, kPi);
}

void DX12Canvas::drawRoundedRectangleOutline(const DFRect& rect, float radius, const DFColor& color, float thickness)
{
    if (rect.width <= 0.0f || rect.height <= 0.0f || thickness <= 0.0f) {
        return;
    }

    const float maxRadius = std::min(rect.width, rect.height) * 0.5f;
    const float r = std::clamp(radius, 0.0f, maxRadius);
    const float t = std::max(0.5f, thickness);

    if (r <= 0.01f) {
        drawRectangle({rect.x, rect.y, rect.width, t}, color);
        drawRectangle({rect.x, rect.y + std::max(0.0f, rect.height - t), rect.width, t}, color);
        drawRectangle({rect.x, rect.y, t, rect.height}, color);
        drawRectangle({rect.x + std::max(0.0f, rect.width - t), rect.y, t, rect.height}, color);
        return;
    }

    drawLine({rect.x + r, rect.y}, {rect.x + rect.width - r, rect.y}, color, t);
    drawLine({rect.x + r, rect.y + rect.height}, {rect.x + rect.width - r, rect.y + rect.height}, color, t);
    drawLine({rect.x, rect.y + r}, {rect.x, rect.y + rect.height - r}, color, t);
    drawLine({rect.x + rect.width, rect.y + r}, {rect.x + rect.width, rect.y + rect.height - r}, color, t);

    const int segments = std::max(8, static_cast<int>(std::ceil(r)));
    auto drawArc = [&](float cx, float cy, float startAngle, float endAngle) {
        DFPoint previous{cx + std::cos(startAngle) * r, cy + std::sin(startAngle) * r};
        for (int i = 1; i <= segments; ++i) {
            const float tNorm = static_cast<float>(i) / static_cast<float>(segments);
            const float angle = startAngle + (endAngle - startAngle) * tNorm;
            DFPoint current{cx + std::cos(angle) * r, cy + std::sin(angle) * r};
            drawLine(previous, current, color, t);
            previous = current;
        }
    };

    drawArc(rect.x + r, rect.y + r, kPi, kPi * 1.5f);
    drawArc(rect.x + rect.width - r, rect.y + r, kPi * 1.5f, kPi * 2.0f);
    drawArc(rect.x + rect.width - r, rect.y + rect.height - r, 0.0f, kPi * 0.5f);
    drawArc(rect.x + r, rect.y + rect.height - r, kPi * 0.5f, kPi);
}

void DX12Canvas::drawLine(const DFPoint& a, const DFPoint& b, const DFColor& color, float thickness)
{
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.0001f) {
        const float t = std::max(1.0f, thickness);
        drawRectangle({a.x - t * 0.5f, a.y - t * 0.5f, t, t}, color);
        return;
    }

    const float nx = -dy / len;
    const float ny = dx / len;
    const float half = std::max(0.5f, thickness * 0.5f);

    const D3DVertex v1{{a.x + nx * half, a.y + ny * half}, {color.r, color.g, color.b, color.a}};
    const D3DVertex v2{{a.x - nx * half, a.y - ny * half}, {color.r, color.g, color.b, color.a}};
    const D3DVertex v3{{b.x + nx * half, b.y + ny * half}, {color.r, color.g, color.b, color.a}};
    const D3DVertex v4{{b.x - nx * half, b.y - ny * half}, {color.r, color.g, color.b, color.a}};

    if (vertices_.size() + 6 > MAX_VERTICES) flush();
    vertices_.push_back(v1); vertices_.push_back(v2); vertices_.push_back(v3);
    vertices_.push_back(v2); vertices_.push_back(v4); vertices_.push_back(v3);
}

void DX12Canvas::flush()
{
    if (vertices_.empty()) return;

    D3D12_RANGE readRange{0, 0};
    uint8_t* data = nullptr;
    vertexBuffer_->Map(0, &readRange, reinterpret_cast<void**>(&data));
    std::memcpy(data, vertices_.data(), vertices_.size() * sizeof(D3DVertex));
    vertexBuffer_->Unmap(0, nullptr);

    struct ViewCB { float screenSize[2]; float pad[2]; } cb{{targetWidth_, targetHeight_}, {0,0}};
    commandList_->SetGraphicsRootSignature(rootSignature_.Get());
    commandList_->SetPipelineState(pipelineState_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList_->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList_->SetGraphicsRoot32BitConstants(0, 2, cb.screenSize, 0);
    commandList_->DrawInstanced(static_cast<UINT>(vertices_.size()), 1, 0, 0);

    vertices_.clear();
}

void DX12Canvas::clear()
{
    vertices_.clear();
}


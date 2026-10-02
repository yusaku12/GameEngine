#include "Pch.h"
#include "Graphics\Renderer\RenderQueue.h"

namespace
{
    using namespace Engine;

    int failures = 0;

    void check(const bool condition, const char* const name)
    {
        if (!condition)
        {
            std::fprintf(stderr, "FAIL: %s\n", name);
            ++failures;
        }
    }

    void checkBatchBoundary(const RenderItem& first, const RenderItem& second, const char* const name)
    {
        RenderQueue queue;
        check(queue.submit(first) && queue.submit(second), "Valid items are submitted");
        check(queue.getInstanceBatchSize(0) == 1, name);
    }
}

int main()
{
    using namespace Engine;
    D3D12_VERTEX_BUFFER_VIEW vertices{};
    D3D12_VERTEX_BUFFER_VIEW otherVertices{};
    D3D12_INDEX_BUFFER_VIEW indices{};
    D3D12_INDEX_BUFFER_VIEW otherIndices{};
    RenderItem item{
        .vertexBuffer = &vertices,
        .indexBuffer = &indices,
        .indexCount = 3,
        .material = { 1, 1 },
    };

    RenderQueue queue;
    check(queue.getInstanceBatchSize(0) == 0, "Empty queue has no batches");
    for (std::uint32_t index = 0; index < 100; ++index)
    {
        item.objectID = index;
        item.worldMatrix = Matrix::CreateTranslation(static_cast<float>(index), 0.0f, 0.0f);
        check(queue.submit(item), "Static instance is submitted");
    }
    queue.sort();
    check(queue.getInstanceBatchSize(0) == 100, "100 static instances form one batch");
    check(queue.getInstanceBatchSize(99) == 1, "Last instance is in bounds");
    check(queue.getInstanceBatchSize(100) == 0, "Out of range index has no batch");

    for (const RenderPassType pass : { RenderPassType::DepthOnly, RenderPassType::Shadow, RenderPassType::AlphaTest })
    {
        queue.clear();
        item.pass = pass;
        check(queue.submit(item) && queue.submit(item), "Pass items are submitted");
        queue.sort();
        check(queue.getInstanceBatchSize(0) == 2, "Depth, shadow and alpha test support instancing");
    }
    item.pass = RenderPassType::Opaque;

    RenderItem changed = item;
    changed.vertexBuffer = &otherVertices;
    checkBatchBoundary(item, changed, "Different vertex buffers do not batch");
    changed = item;
    changed.indexBuffer = &otherIndices;
    checkBatchBoundary(item, changed, "Different index buffers do not batch");
    changed = item;
    ++changed.indexStart;
    checkBatchBoundary(item, changed, "Different index ranges do not batch");
    changed = item;
    ++changed.indexCount;
    checkBatchBoundary(item, changed, "Different index counts do not batch");
    changed = item;
    ++changed.baseVertex;
    checkBatchBoundary(item, changed, "Different base vertices do not batch");
    changed = item;
    ++changed.material.index;
    checkBatchBoundary(item, changed, "Different materials do not batch");
    changed = item;
    ++changed.material.generation;
    checkBatchBoundary(item, changed, "Different material generations do not batch");
    changed = item;
    ++changed.shaderVariantID;
    checkBatchBoundary(item, changed, "Different shader variants do not batch");
    changed = item;
    ++changed.pipelineID;
    checkBatchBoundary(item, changed, "Different pipelines do not batch");
    changed = item;
    ++changed.textureID;
    checkBatchBoundary(item, changed, "Different textures do not batch");
    changed = item;
    changed.surfaceType = MaterialSurfaceType::AlphaTest;
    checkBatchBoundary(item, changed, "Different surface types do not batch");
    changed = item;
    changed.pass = RenderPassType::Shadow;
    checkBatchBoundary(item, changed, "Different passes do not batch");
    changed = item;
    check(changed.materialProperties.setFloat(MaterialParameters::Metallic, 0.5f), "Set material override");
    checkBatchBoundary(item, changed, "Candidate with material override does not batch");
    checkBatchBoundary(changed, item, "First item with material override does not batch");
    changed = item;
    changed.skinningPalette = std::make_shared<SkinningPaletteSnapshot>();
    checkBatchBoundary(item, changed, "Candidate with skinning does not batch");
    checkBatchBoundary(changed, item, "First item with skinning does not batch");

    queue.clear();
    item.pass = RenderPassType::Transparent;
    item.objectID = 1;
    item.cameraDepth = 1.0f;
    check(queue.submit(item), "Near transparent item submitted");
    item.objectID = 2;
    item.cameraDepth = 100.0f;
    check(queue.submit(item), "Far transparent item submitted");
    queue.sort();
    check(queue.getItems()[0].objectID == 2, "Transparency remains back-to-front");
    check(queue.getInstanceBatchSize(0) == 1, "Transparent draws remain individual");

    queue.clear();
    item.cameraDepth = 10.0f;
    item.vertexBuffer = &otherVertices;
    item.meshID = 0;
    item.objectID = 2;
    check(queue.submit(item), "First equal-depth transparent item submitted");
    item.vertexBuffer = &vertices;
    item.meshID = 1;
    item.objectID = 1;
    check(queue.submit(item), "Second equal-depth transparent item submitted");
    queue.sort();
    check(queue.getItems()[0].objectID == 2, "Equal-depth transparency retains the original state tie-break");

    queue.clear();
    item.pass = RenderPassType::Opaque;
    item.meshID = 0;
    item.vertexBuffer = &vertices;
    item.objectID = 1;
    check(queue.submit(item), "First geometry submitted");
    item.vertexBuffer = &otherVertices;
    item.objectID = 2;
    check(queue.submit(item), "Other geometry submitted");
    item.vertexBuffer = &vertices;
    item.objectID = 3;
    check(queue.submit(item), "Repeated first geometry submitted");
    queue.sort();
    std::size_t batches = 0;
    std::size_t instances = 0;
    while (instances < queue.getItems().size())
    {
        const std::size_t size = queue.getInstanceBatchSize(instances);
        check(size != 0, "Batch traversal always advances");
        if (size == 0)
            break;
        instances += size;
        ++batches;
    }
    check(batches == 2 && instances == 3, "Sorting groups real geometry rather than mesh index");
    check(queue.getStatistics().visibleItems == 3, "Statistics retain all instances");

    std::printf("RenderQueue tests: %s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

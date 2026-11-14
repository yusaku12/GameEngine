#include "Pch.h"
#include "Graphics\Debug\DebugPrimitiveCulling.h"

#include <cassert>

int main()
{
    using namespace Engine;
    const CameraFrustum orthographic{
        { { { 1, 0, 0, 1 }, { -1, 0, 0, 1 }, { 0, 1, 0, 1 },
            { 0, -1, 0, 1 }, { 0, 0, 1, 0 }, { 0, 0, -1, 10 } } }, true };
    const CameraFrustum perspective{
        { { { 1, 0, 1, 0 }, { -1, 0, 1, 0 }, { 0, 1, 1, 0 },
            { 0, -1, 1, 0 }, { 0, 0, 1, -1 }, { 0, 0, -1, 10 } } }, true };

    assert(!DebugPrimitiveCulling::isVisibleLine(orthographic, { 2, 0, 2 }, { 3, 0, 2 }));
    assert(DebugPrimitiveCulling::isVisibleLine(orthographic, { -2, 0, 2 }, { 2, 0, 2 }));
    assert(DebugPrimitiveCulling::isVisibleLine(orthographic, { 1, 0, 2 }, { 1, 1, 2 }));
    assert(!DebugPrimitiveCulling::isVisibleLine(perspective, { 0, 0, -2 }, { 0, 0, 0 }));
    assert(DebugPrimitiveCulling::isVisibleLine(perspective, { 0, 0, 0 }, { 0, 0, 2 }));
    assert(!DebugPrimitiveCulling::isVisibleLine(perspective, { 7, 0, 2 }, { 8, 0, 2 }));

    const Vector3 extents(1, 0.5f, 0.5f);
    assert(!DebugPrimitiveCulling::isVisibleBox(orthographic, Matrix::CreateTranslation(4, 0, 2), extents));
    assert(DebugPrimitiveCulling::isVisibleBox(orthographic, Matrix::CreateTranslation(2, 0, 2), extents));
    const Matrix rotated = Matrix::CreateScale(2, 1, 1) * Matrix::CreateRotationZ(DirectX::XM_PIDIV2)
        * Matrix::CreateTranslation(0, 2.5f, 2);
    assert(DebugPrimitiveCulling::isVisibleBox(orthographic, rotated, extents));
    assert(!DebugPrimitiveCulling::isVisibleBox(perspective, Matrix::CreateTranslation(0, 0, -3), extents));
    assert(DebugPrimitiveCulling::isVisibleBox(perspective, Matrix::CreateTranslation(0, 0, 1), extents));

    CameraFrustum invalid;
    assert(DebugPrimitiveCulling::isVisibleBox(invalid, Matrix::CreateTranslation(100, 0, 0), extents));
    Matrix projective = Matrix::CreateTranslation(100, 0, 0);
    projective._14 = 1.0f;
    assert(DebugPrimitiveCulling::isVisibleBox(orthographic, projective, extents));
    assert(DebugPrimitiveCulling::isVisibleBox(orthographic, Matrix::Identity,
        Vector3((std::numeric_limits<float>::infinity)(), 1, 1)));
}

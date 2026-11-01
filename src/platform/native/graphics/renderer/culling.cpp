#include "renderdistance.h"
#include "renderer.h"

#include "../ee/vu.h"
#include "../hardware.h"

#include "game/math.h"
#include "game/scenery.h"
#include "platform/graphics.h"

// The chunks' culling runs on VU0: its microprograms of the second set test what the scenery draws against the view loaded into
// its memory (the frustum's planes as columns from 0x80, the view's matrices from 0x98) and leave the outcome in its registers
// (vi01 set when it's out of the view, vi02 when it's partly out, the level of detail's distance in vf15's x, the matrices it
// worked out in vf07-vf10 and vf11-vf14); macro mode clips a box's corners against the view's matrix

EABI_EXPORT(FUN_00201368, ViewFrustumPlanes);
EABI_EXPORT(FUN_00201480, FrustumPlanes);
EABI_EXPORT(FUN_001ef410, Platform::Graphics::SetViewFrustum);


namespace
{
// Where the culling's data goes in VU0's memory
constexpr s32 PlanesAddress = 0x80;
constexpr s32 ViewAddress = 0x98;
// A plane columns' quadwords
constexpr s32 PlaneColumnRows = sizeof(PlaneColumns) / sizeof(Vector4);
// The far set's sides are five times as far out
constexpr f32 FarSidesScale = 5.0f;
}

extern "C"
{
BigVu0Packet* StartBigVu0Packet(BigVu0Packet* packet)
{
    packet->capacity = BigVu0Packet::Capacity;
    packet->count = 0;
    packet->unused04 = 0;
    return packet;
}

// A plane's normal (and its absolute value) and offset put in a column of the four
void SetPlaneColumn(PlaneColumns* columns, const Vector4* plane, s32 column)
{
    Vector4 normal = *plane;
    Vector4 absolute = normal;
    absolute.x = __builtin_fabsf(absolute.x);
    absolute.y = __builtin_fabsf(absolute.y);
    absolute.z = __builtin_fabsf(absolute.z);
    const f32* from = &absolute.x;
    const f32* signedFrom = &normal.x;
    for (s32 axis = 0; axis < 3; axis++)
    {
        columns->absoluteNormals[axis][column] = from[axis];
        columns->normals[axis][column] = signedFrom[axis];
    }

    columns->offsets[column] = plane->w;
}

void SetSidePlaneColumns(PlaneColumns* columns, const Vector4* planes)
{
    for (u32 column = 0; column < SidePlaneCount; column++)
    {
        SetPlaneColumn(columns, &planes[column + FirstSidePlane], static_cast<s32>(column));
    }
}

void AddPlaneColumns(const PlaneColumns* columns, BigVu0Packet* packet)
{
    const auto* rows = reinterpret_cast<const Vector4*>(columns);
    for (s32 row = 0; row < PlaneColumnRows; row++)
    {
        *reinterpret_cast<Vector4*>(packet->data[packet->count]) = rows[row];
        packet->unused04 = 0;
        packet->count++;
    }
}

void ClearPlaneColumns(PlaneColumns* columns)
{
    auto* rows = reinterpret_cast<Vector4*>(columns);
    for (s32 row = 0; row < PlaneColumnRows; row++)
    {
        rows[row] = {0.0f, 0.0f, 0.0f, 0.0f};
    }
}





// The frustum's planes through the eye and the near rectangle's corners, the far plane the near one moved the depth along it
// and turned around
void FrustumPlanes(f32 depth, Vector4* planes, const Vector4* eye, const Vector4* topRight, const Vector4* bottomRight,
                   const Vector4* bottomLeft, const Vector4* topLeft)
{
    PlaneFromTriangle(&planes[NearPlane], topRight, bottomLeft, bottomRight);
    Vector4 along = {0.0f, 0.0f, depth, 1.0f};
    PlaneAlongNormal(&planes[NearPlane], &along, &planes[FarPlane]);
    planes[FarPlane].x = -planes[FarPlane].x;
    planes[FarPlane].y = -planes[FarPlane].y;
    planes[FarPlane].z = -planes[FarPlane].z;
    planes[FarPlane].w = -planes[FarPlane].w;
    // The sides: right, left, top and bottom
    PlaneFromTriangle(&planes[FirstSidePlane], eye, bottomRight, topRight);
    PlaneFromTriangle(&planes[FirstSidePlane + 1], eye, topLeft, bottomLeft);
    PlaneFromTriangle(&planes[FirstSidePlane + 2], eye, topRight, topLeft);
    PlaneFromTriangle(&planes[FirstSidePlane + 3], eye, bottomLeft, bottomRight);
}

// The near rectangle's half sizes are the near distance times the tangent of half the field of view (and the aspect across),
// times the scale
void ViewFrustumPlanes(f32 near, f32 far, f32 aspect, f32 scale, Vector4* planes, const s32* fieldOfView)
{
    s32 half = static_cast<s32>(static_cast<f32>(*fieldOfView) * 0.5f);
    f32 height = near * TanOfAngle(&half);
    f32 width = height * aspect * scale;
    height = height * scale;
    Vector4 topRight = {width, height, near, 1.0f};
    Vector4 topLeft = {-width, height, near, 1.0f};
    Vector4 bottomRight = {width, -height, near, 1.0f};
    Vector4 bottomLeft = {-width, -height, near, 1.0f};
    Vector4 eye = {0.0f, 0.0f, 0.0f, 1.0f};
    FrustumPlanes(far - near, planes, &eye, &topRight, &bottomRight, &bottomLeft, &topLeft);
}

// The view's planes taken into a space: the side planes as columns, the near and far ones as they are
void ViewPlanesIn(const Matrix4x4* matrix, PlaneColumns* columns, Vector4* nearAndFar)
{
    Vector4 planes[FrustumPlaneCount];
    for (u32 plane = 0; plane < FrustumPlaneCount; plane++)
    {
        planes[plane] = g_ViewPlanes[plane];
    }

    TransformPlanes(planes, matrix);
    SetSidePlaneColumns(columns, planes);
    nearAndFar[0] = planes[NearPlane];
    nearAndFar[1] = planes[FarPlane];
}

void FarViewPlanesIn(const Matrix4x4* matrix, PlaneColumns* columns)
{
    Vector4 planes[FrustumPlaneCount];
    for (u32 plane = 0; plane < FrustumPlaneCount; plane++)
    {
        planes[plane] = g_FarViewPlanes[plane];
    }

    TransformPlanes(planes, matrix);
    SetSidePlaneColumns(columns, planes);
}

// The far set's side planes taken into a space as rows, and the near plane
u32* WriteChunkViewRows(u32* nearPlane, const Matrix4x4* matrix, u32* rows)
{
    auto* row = reinterpret_cast<Vector4*>(rows);
    for (s32 plane = FirstSidePlane; plane < static_cast<s32>(FarPlane); plane++)
    {
        TransformPlaneOf(g_FarViewPlanes, plane, matrix, row);
        row++;
    }

    Vector4 near;
    TransformPlaneOf(g_ViewPlanes, NearPlane, matrix, &near);
    *reinterpret_cast<Vector4*>(nearPlane) = near;
    return nearPlane;
}

// The particles' view: the frustum's planes in the chunk's space (the sides as columns, then the near and far planes as columns),
// the camera's place and the second matrix, sent as the index'th view
s32 UploadParticleView(Matrix4x4* matrices, s32 index)
{
    PlaneColumns columns;
    Vector4 nearAndFar[2];
    BigVu0Packet packet;
    ViewPlanesIn(&matrices[0], &columns, nearAndFar);
    StartBigVu0Packet(&packet);
    AddPlaneColumns(&columns, &packet);
    ClearPlaneColumns(&columns);
    SetPlaneColumn(&columns, &nearAndFar[0], 0);
    SetPlaneColumn(&columns, &nearAndFar[1], 1);
    AddPlaneColumns(&columns, &packet);
    // The camera's place (its matrix's fourth row) and the second matrix
    *reinterpret_cast<Vector4*>(packet.data[packet.count]) = *RowOf(&matrices[0], 3);
    packet.unused04 = 0;
    packet.count++;
    for (s32 row = 0; row < 4; row++)
    {
        *reinterpret_cast<Vector4*>(packet.data[packet.count + row]) = *RowOf(&matrices[1], row);
    }

    packet.unused04 = 0;
    packet.count += 4;
    s32 address = index * packet.count;
    SendToVu0(g_Vu0Programs, packet.data, packet.count, address);
    *reinterpret_cast<s32*>(&matrices[4]) = address;
    return address;
}

// A box's eight corners through the view's matrix (vf10-vf13) and scaled by its first guard row (vf09), clipped: the flags of
// either set or of every corner, the corners' outside bits taken together (the scaled ones in the low six) and their common bits
void ClipCorners(u32* flags)
{
    // Native: VU0's macro mode on the emulated VU0, in the asm's order (each corner vf01-vf08 through the matrix vf10-vf13 into
    // itself, scaled by vf09 into vf17-vf24, both clipped; the clip flag's last twelve bits taken after each pair)
    Ee::Vu& vu = *NativeGraphics::GetHardware().vu0;
    u32 any = 0;
    u32 every = 0xFFFFFFFF;
    for (u32 corner = 1; corner <= 8; corner++)
    {
        vu.MacroMulBc(0, 10, corner, 0, true);
        vu.MacroMaddBc(0, 11, corner, 1, true);
        vu.MacroMaddBc(0, 12, corner, 2, true);
        vu.MacroMaddBc(corner, 13, 0, 3, false);
        vu.MacroMul(16 + corner, corner, 9);
        vu.MacroClipW(corner, corner);
        vu.MacroClipW(16 + corner, 16 + corner);
        u32 clip = vu.clipFlag;
        any |= clip;
        every &= clip;
    }

    flags[0] = any;
    flags[1] = every;
}

// The box's corners: each takes the box's maximum in one axis (the first three) or its minimum in one axis (the last three) and
// the other end in the others, the two corners themselves first
static void LoadBoxCorners(const Box* box)
{
    Ee::Vu& vu = *NativeGraphics::GetHardware().vu0;
    const auto* corners = reinterpret_cast<const u8*>(box);
    vu.LoadVf(1, corners);
    vu.LoadVf(2, corners + 0x10);
    vu.MacroMove(3, 1, 0xF);
    vu.MacroMove(4, 1, 0xF);
    vu.MacroMove(5, 1, 0xF);
    vu.MacroMove(6, 2, 0xF);
    vu.MacroMove(7, 2, 0xF);
    vu.MacroMove(8, 2, 0xF);
    vu.MacroMove(3, 2, 1);
    vu.MacroMove(4, 2, 2);
    vu.MacroMove(5, 2, 4);
    vu.MacroMove(6, 1, 1);
    vu.MacroMove(7, 1, 2);
    vu.MacroMove(8, 1, 4);
}

}

namespace Platform::Graphics
{
const Vector4* ViewPlanes()
{
    return g_ViewPlanes;
}

void ClipBoxAt(const Matrix4x4* view, const Box* box, u32* flags, const Matrix4x4* model)
{
    LoadBoxCorners(box);
    Ee::Vu& vu = *NativeGraphics::GetHardware().vu0;
    const auto* v = reinterpret_cast<const u8*>(view);
    const auto* m = reinterpret_cast<const u8*>(model);
    vu.LoadVf(9, v + 0x40);
    for (u32 row = 0; row < 4; row++)
    {
        vu.LoadVf(24 + row, v + row * 0x10);
        vu.LoadVf(28 + row, m + row * 0x10);
    }

    for (u32 row = 0; row < 4; row++)
    {
        vu.MacroMulBc(0, 24, 28 + row, 0, true);
        vu.MacroMaddBc(0, 25, 28 + row, 1, true);
        vu.MacroMaddBc(0, 26, 28 + row, 2, true);
        vu.MacroMaddBc(10 + row, 27, 28 + row, 3, false);
    }
    ClipCorners(flags);
}

void ClipBox(const Matrix4x4* view, const Box* box, u32* flags)
{
    LoadBoxCorners(box);
    Ee::Vu& vu = *NativeGraphics::GetHardware().vu0;
    const auto* v = reinterpret_cast<const u8*>(view);
    vu.LoadVf(9, v + 0x40);
    for (u32 row = 0; row < 4; row++)
    {
        vu.LoadVf(10 + row, v + row * 0x10);
    }
    ClipCorners(flags);
}

void SetViewFrustum(f32 near, f32 far, f32 aspect, const s32* fieldOfView)
{
    // The draw distance option's far plane, the projection's (renderdistance.h)
    far = std::max(far * Native::FarPlaneScale(), near + 1.0f);
    s32 angle = *fieldOfView;
    ViewFrustumPlanes(near, far, aspect, 1.0f, g_ViewPlanes, &angle);
    angle = *fieldOfView;
    ViewFrustumPlanes(near, far, aspect, FarSidesScale, g_FarViewPlanes, &angle);
}

void LoadChunkPlanes(const Matrix4x4* place)
{
    PlaneColumns columns;
    Vector4 nearAndFar[2];
    BigVu0Packet packet;
    ViewPlanesIn(place, &columns, nearAndFar);
    StartBigVu0Packet(&packet);
    AddPlaneColumns(&columns, &packet);
    FarViewPlanesIn(place, &columns);
    AddPlaneColumns(&columns, &packet);
    ClearPlaneColumns(&columns);
    SetPlaneColumn(&columns, &nearAndFar[0], 0);
    SetPlaneColumn(&columns, &nearAndFar[1], 1);
    AddPlaneColumns(&columns, &packet);
    SendToVu0(g_Vu0Programs, packet.data, packet.count, PlanesAddress);
}

void LoadPortalPlanes(const Matrix4x4* place, const Vector4* portal)
{
    PlaneColumns columns;
    Vector4 near;
    BigVu0Packet packet;
    SetSidePlaneColumns(&columns, portal);
    StartBigVu0Packet(&packet);
    AddPlaneColumns(&columns, &packet);
    FarViewPlanesIn(place, &columns);
    AddPlaneColumns(&columns, &packet);
    ClearPlaneColumns(&columns);
    TransformPlaneOf(g_ViewPlanes, NearPlane, place, &near);
    SetPlaneColumn(&columns, &near, 0);
    AddPlaneColumns(&columns, &packet);
    SendToVu0(g_Vu0Programs, packet.data, packet.count, PlanesAddress);
}

// The view's matrix, the renderer view's world to screen matrix and the camera's place, the second set selected and the model
// and link matrices made the identity
void LoadCullingView(const Matrix4x4* toClip, const Matrix4x4* toScreen, const Vector4* camera)
{
    BigVu0Packet packet;
    StartBigVu0Packet(&packet);
    for (s32 row = 0; row < 4; row++)
    {
        *reinterpret_cast<Vector4*>(packet.data[packet.count + row]) = reinterpret_cast<const Vector4*>(toClip)[row];
    }

    packet.unused04 = 0;
    packet.count += 4;
    for (s32 row = 0; row < 4; row++)
    {
        *reinterpret_cast<Vector4*>(packet.data[packet.count + row]) = reinterpret_cast<const Vector4*>(toScreen)[row];
    }

    packet.unused04 = 0;
    packet.count += 4;
    *reinterpret_cast<Vector4*>(packet.data[packet.count]) = *camera;
    packet.unused04 = 0;
    packet.count++;
    SendToVu0(g_Vu0Programs, packet.data, packet.count, ViewAddress);
    SelectVu0Programs(g_Vu0Programs, CullingPrograms, true);
    Matrix4x4 identity;
    InitIdentityMatrix(&identity);
    CullLoadLink(&identity, &identity);
}

void CullLoadLink(const Matrix4x4* chunk, const Matrix4x4* object)
{
    Ee::Vu& vu = *NativeGraphics::GetHardware().vu0;
    for (u32 row = 0; row < 4; row++)
    {
        vu.LoadVf(3 + row, reinterpret_cast<const u8*>(chunk) + row * 0x10);
        vu.LoadVf(7 + row, reinterpret_cast<const u8*>(object) + row * 0x10);
    }

    vu.CallMicro(0x398);
}

void CullLoadModel(const Matrix4x4* model)
{
    Ee::Vu& vu = *NativeGraphics::GetHardware().vu0;
    for (u32 row = 0; row < 4; row++)
    {
        vu.LoadVf(3 + row, reinterpret_cast<const u8*>(model) + row * 0x10);
    }

    vu.CallMicro(0x2D0);
}

void CullTestBox(const Vector4* corners)
{
    Ee::Vu& vu = *NativeGraphics::GetHardware().vu0;
    vu.LoadVf(1, corners);
    vu.LoadVf(2, corners + 1);
    vu.CallMicro(0x1C0);
}

// Loaded through the integer registers after the wait, like the box of CullTestBox: the last test may still be using them
void CullTestBoxAt(const Box* box, const Matrix4x4* model)
{
    Ee::Vu& vu = *NativeGraphics::GetHardware().vu0;
    const auto* b = reinterpret_cast<const u8*>(box);
    const auto* m = reinterpret_cast<const u8*>(model);
    vu.LoadVf(1, b);
    vu.LoadVf(2, b + 0x10);
    vu.LoadVf(3, m);
    vu.LoadVf(4, m + 0x10);
    vu.LoadVf(5, m + 0x20);
    vu.LoadVf(6, m + 0x30);
    vu.CallMicro(0x500);
}

CullOutcome CullResult()
{
    // vf15's x (its low word, sign extended by the asm's sll), vi02 and vi01
    Ee::Vu& vu = *NativeGraphics::GetHardware().vu0;
    u32 outside = vu.vi[1];
    u32 clipped = vu.vi[2];
    u32 distance = vu.vf[15].u[0];
    return {outside, clipped, static_cast<s32>(distance)};
}

void CullMatrices(Matrix4x4* clipped, Matrix4x4* toScreen)
{
    Ee::Vu& vu = *NativeGraphics::GetHardware().vu0;
    for (u32 row = 0; row < 4; row++)
    {
        if (clipped != nullptr)
        {
            vu.StoreVf(7 + row, reinterpret_cast<u8*>(clipped) + row * 0x10);
        }

        vu.StoreVf(11 + row, reinterpret_cast<u8*>(toScreen) + row * 0x10);
    }
}
}

#include "stdafx.h"
#include "camera.h"

Camera * Camera::camera_ = nullptr;
Camera * Camera::Get () { return camera_; }

Camera::Camera () {
    Reset();
    camera_ = this;
}
Camera::~Camera () {
    camera_ = nullptr;
}
void Camera::Get3DViewProjMatrices (
    XMFLOAT4X4 * view,
    XMFLOAT4X4 * proj,
    float fov_degrees,
    float screen_width,
    float screen_height
) {
    float aspect_ratio =
        (float)screen_width / (float)screen_height;
    float fov_y = fov_degrees * XM_PI / 180.0f;
    if (aspect_ratio < 1.0f)
        fov_y /= aspect_ratio;

    auto view_transpose_xmmat =
        XMMatrixTranspose(XMMatrixLookAtRH(eye_, lookat_, up_));
    XMStoreFloat4x4(view, view_transpose_xmmat);

    auto proj_transpose_xmmat = XMMatrixTranspose(
        XMMatrixPerspectiveFovRH(fov_y, aspect_ratio, 0.01f, 125.0f));
    XMStoreFloat4x4(proj, proj_transpose_xmmat);
}
void Camera::GetOrthoProjMatrices (
    XMFLOAT4X4 * view,
    XMFLOAT4X4 * proj,
    float width,
    float height
) {
    auto view_transpose_xmmat =
        XMMatrixTranspose(XMMatrixLookAtRH(eye_, lookat_, up_));
    XMStoreFloat4x4(view, view_transpose_xmmat);

    auto proj_transpose_xmmat = XMMatrixTranspose(
        XMMatrixOrthographicRH(width, height, 0.01f, 125.0f));
    XMStoreFloat4x4(proj, proj_transpose_xmmat);
}
void Camera::Reset () {
    eye_ = XMVectorSet(0.0f, 15.0f, -30.0f, 0.0f);
    lookat_ = XMVectorSet(0.0f, 8.0f, 0.0f, 0.0f);
    up_ = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
}
void Camera::Set (XMVECTOR eye, XMVECTOR at, XMVECTOR up) {
    eye_ = eye;
    lookat_ = at;
    up_ = up;
}
void Camera::RotateYaw (float deg) {
    XMMATRIX rotation = XMMatrixRotationAxis(up_, deg);
    eye_ = XMVector3TransformCoord(eye_, rotation);
}
void Camera::RotatePitch (float deg) {
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(eye_, up_));
    XMMATRIX rotation = XMMatrixRotationAxis(right, deg);
    eye_ = XMVector3TransformCoord(eye_, rotation);
}


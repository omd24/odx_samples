#pragma once

using namespace DirectX;

struct Camera {
private:
    static Camera * camera_;
public:
    XMVECTOR eye_;      // -- cam dir in world space
    XMVECTOR lookat_;   // -- target dir in world space
    XMVECTOR up_;       // -- up direction
    Camera ();
    ~Camera ();

    void Get3DViewProjMatrices (
        XMFLOAT4X4 * view,
        XMFLOAT4X4 * proj,
        float fov_degrees,
        float screen_width,
        float screen_height
    );
    void Reset ();
    void Set (XMVECTOR eye, XMVECTOR at, XMVECTOR up);
    static Camera * Get ();
    void RotateYaw (float deg);
    void RotatePitch (float deg);
    void GetOrthoProjMatrices (
        XMFLOAT4X4 * view,
        XMFLOAT4X4 * proj,
        float width,
        float height
    );
};



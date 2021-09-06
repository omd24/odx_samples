
#include "stdafx.h"

#include "odx_helper.h"

int main() {

    auto ret = CalculateCBufferByteSize(1000);
    auto constexpr x = sizeof(DXGI_FORMAT_R32G32B32A32_FLOAT);
    auto constexpr xx = sizeof(float);
    auto constexpr xxx = sizeof(double);


    return(0);
}

#include "pch.h"
#include "Platform/Candy.h"

namespace Lucida::Candy
{
    bool HighContrast() noexcept
    {
        HIGHCONTRASTW contrast{ sizeof(contrast) };
        return ::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) && (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    }
}

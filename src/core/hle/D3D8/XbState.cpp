// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
// ******************************************************************
// *
// *  This file is part of the Cxbx project.
// *
// *  Cxbx and Cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file COPYING.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
// *
// *  All rights reserved
// *
// ******************************************************************
#define _XBOXKRNL_DEFEXTRN_
#define LOG_PREFIX CXBXR_MODULE::D3DST

#include "core\kernel\support\Emu.h"
#include "core\kernel\support\EmuXTL.h"

// deferred state lookup tables
DWORD *XTL::EmuD3DDeferredRenderState = nullptr;
DWORD *XTL::EmuD3DDeferredTextureState = nullptr;

extern uint32_t g_BuildVersion;

#include "core\hle\Intercept.hpp" // For g_SymbolAddresses

void VerifyAndFixEmuDeferredRenderStateOffset()
{
    // Verify that EmuD3DDeferredRenderState is correct, if not, we can programatically correct it
    // We should also flag up a warning so this can be fixed upstream in XboxSymbolDatabase!
    // This is made possible by the registration of D3DRS_CULLMODE by XboxSymbolDatabase
    static bool verifiedRenderStateOffset = false;
    if (verifiedRenderStateOffset) {
        return;
    }

    verifiedRenderStateOffset = true;

    DWORD CullModeOffset = g_SymbolAddresses["D3DRS_CULLMODE"];

    // If we found a valid CullMode offset, verify the symbol location
    if (CullModeOffset == 0) {
        EmuLog(LOG_LEVEL::WARNING, "D3DRS_CULLMODE could not be found. Please update the XbSymbolDatabase submodule");
        return;
    }

    // Calculate index of D3DRS_CULLMODE for this XDK. We start counting from the first deferred state (D3DRS_FOGENABLE)
    DWORD CullModeIndex = 0;
    for (int i = XTL::X_D3DRS_FOGENABLE; i < XTL::X_D3DRS_CULLMODE; i++) {
        if (XTL::DxbxRenderStateInfo[i].V <= g_BuildVersion) {
            CullModeIndex++;
        }
    }

    // If the offset was incorrect, calculate the correct offset, log it, and fix it
    if ((DWORD)(&XTL::EmuD3DDeferredRenderState[CullModeIndex]) != CullModeOffset) {
        DWORD CorrectOffset = CullModeOffset - (CullModeIndex * sizeof(DWORD));
        EmuLog(LOG_LEVEL::WARNING, "EmuD3DDeferredRenderState returned by XboxSymbolDatabase (0x%08X) was incorrect. Correcting to be 0x%08X.\nPlease file an issue with the XbSymbolDatabase project", XTL::EmuD3DDeferredRenderState, CorrectOffset);
        XTL::EmuD3DDeferredRenderState = (DWORD*)CorrectOffset;
    }
}

void UpdateDeferredRenderStates()
{
    // Certain D3DRS values need to be checked on each Draw[Indexed]Vertices
    if (XTL::EmuD3DDeferredRenderState != 0) {
        // Loop through all deferred render states
        for (unsigned int RenderState = XTL::X_D3DRS_FOGENABLE; RenderState <= XTL::X_D3DRS_PATCHSEGMENTS; RenderState++) {
            // If this render state does not have a PC counterpart, skip it
            if (XTL::DxbxRenderStateInfo[RenderState].PC == 0) {
                continue;
            }

            uint8_t index = RenderState - XTL::X_D3DRS_FOGENABLE;
            // Some render states require special handling to convert to host, but most can be mapped 1:1
            // We use a switch/case to handle the special states
            switch (RenderState) {
                case XTL::X_D3DRS_WRAP0: {
                    ::DWORD dwConv = 0;

                    dwConv |= (XTL::EmuD3DDeferredRenderState[index] & 0x00000010) ? D3DWRAP_U : 0;
                    dwConv |= (XTL::EmuD3DDeferredRenderState[index] & 0x00001000) ? D3DWRAP_V : 0;
                    dwConv |= (XTL::EmuD3DDeferredRenderState[index] & 0x00100000) ? D3DWRAP_W : 0;

                    g_pD3DDevice->SetRenderState(XTL::D3DRS_WRAP0, dwConv);
                } break;
                case XTL::X_D3DRS_WRAP1: {
                    ::DWORD dwConv = 0;

                    dwConv |= (XTL::EmuD3DDeferredRenderState[index] & 0x00000010) ? D3DWRAP_U : 0;
                    dwConv |= (XTL::EmuD3DDeferredRenderState[index] & 0x00001000) ? D3DWRAP_V : 0;
                    dwConv |= (XTL::EmuD3DDeferredRenderState[index] & 0x00100000) ? D3DWRAP_W : 0;

                    g_pD3DDevice->SetRenderState(XTL::D3DRS_WRAP1, dwConv);
                } break;
                default:
                    g_pD3DDevice->SetRenderState(XTL::DxbxRenderStateInfo[RenderState].PC, XTL::EmuD3DDeferredRenderState[index]);
                    break;
            }
        }
    }
}

DWORD GetDeferredTextureStateFromIndex(DWORD State)
{
    // On early XDKs, we need to shuffle the values around a little
    // TODO: Verify which XDK version this change occurred at
    if (g_BuildVersion <= 3948) {
        // Values range 0-9 (D3DTSS_COLOROP to D3DTSS_TEXTURETRANSFORMFLAGS) become 12-21
        if (State <= 9) {
            return State + 12;
        }

        // All Remaining values 10-21 (D3DTSS_ADDRESSU to D3DTSS_ALPHAKILL) become 0-11
        return State - 10;
    }

    // On later XDKs, the mapping is identical to our representation
    return State;
}

void UpdateDeferredTextureStates()
{
    // Iterate through all deferred texture states/stages

    // Track if we need to overwrite state 0 with 3 because of Point Sprites
    bool pointSpriteOverride = false;
    if (XTL::EmuD3DDeferredRenderState[XTL::X_D3DRS_POINTSPRITEENABLE - XTL::X_D3DRS_FOGENABLE] == TRUE) {
        pointSpriteOverride = true;
    }

    for (int StageIndex = 0; StageIndex < XTL::X_D3DTS_STAGECOUNT; StageIndex++) {
        for (int StateIndex = XTL::X_D3DTSS_DEFERRED_FIRST; StateIndex <= XTL::X_D3DTSS_DEFERRED_LAST; StateIndex++) {
            // Read the value of the current stage/state from the Xbox data structure
            DWORD Value = XTL::EmuD3DDeferredTextureState[(StageIndex * XTL::X_D3DTS_STAGESIZE) + StateIndex];
            
            // Convert the index of the current state to an index that we can use
            // This handles the case when XDKs have different state values
            DWORD State = GetDeferredTextureStateFromIndex(StateIndex);
            DWORD Stage = StageIndex;

            // If point sprites are enabled, we need to overwrite our existing state 0 with State 3 also
            if (pointSpriteOverride && Stage == 3) {
                Stage = 0;

                // Make sure we only do this once
                pointSpriteOverride = false;
                StageIndex--; // Force Stage 3 to repeat, without this hack next time
            }

            switch (State) {
                case XTL::X_D3DTSS_ADDRESSU: case XTL::X_D3DTSS_ADDRESSV: case XTL::X_D3DTSS_ADDRESSW:
                    if (Value == XTL::X_D3DTADDRESS_CLAMPTOEDGE) {
                        EmuLog(LOG_LEVEL::WARNING, "ClampToEdge is unsupported");
                        break;
                    }

                    //  These states match the PC counterpart IDs
                    g_pD3DDevice->SetSamplerState(Stage, (XTL::D3DSAMPLERSTATETYPE)(State + 1), Value);
                    break;
                case XTL::X_D3DTSS_MAGFILTER: case XTL::X_D3DTSS_MINFILTER: case XTL::X_D3DTSS_MIPFILTER:
                    if (Value == XTL::X_D3DTEXF_QUINCUNX) {
                        EmuLog(LOG_LEVEL::WARNING, "QuinCunx is unsupported");
                        break;
                    }

                    //  These states (when incremented by 2) match the PC counterpart IDs
                    g_pD3DDevice->SetSamplerState(Stage, (XTL::D3DSAMPLERSTATETYPE)(State + 2), Value);
                    break;
                case XTL::X_D3DTSS_MIPMAPLODBIAS:
                    g_pD3DDevice->SetSamplerState(Stage, XTL::D3DSAMP_MIPMAPLODBIAS, Value);
                    break;
                case XTL::X_D3DTSS_MAXMIPLEVEL:
                    g_pD3DDevice->SetSamplerState(Stage, XTL::D3DSAMP_MAXMIPLEVEL, Value);
                    break;
                case XTL::X_D3DTSS_MAXANISOTROPY:
                    g_pD3DDevice->SetSamplerState(Stage, XTL::D3DSAMP_MAXANISOTROPY, Value);
                    break;
                case XTL::X_D3DTSS_COLORKEYOP: // Xbox ext
                    // Logging Disabled: Causes Dashboard to slow down massively
                    //EmuLog(LOG_LEVEL::WARNING, "D3DTSS_COLORKEYOP is unsupported");
                    break;
                case XTL::X_D3DTSS_COLORSIGN: // Xbox ext
                    // Logging Disabled: Causes Dashboard to slow down massively
                    //EmuLog(LOG_LEVEL::WARNING, "D3DTSS_COLORSIGN is unsupported");
                    break;
                case XTL::X_D3DTSS_ALPHAKILL: // Xbox ext
                    // Logging Disabled: Causes Dashboard to slow down massively
                    //EmuLog(LOG_LEVEL::WARNING, "D3DTSS_ALPHAKILL is unsupported");
                    break;
                case XTL::X_D3DTSS_COLOROP:
                    // TODO: This would be better split into it's own function, or a lookup array
                    switch (Value) {
                        case XTL::X_D3DTOP_DISABLE:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_DISABLE);
                            break;
                        case XTL::X_D3DTOP_SELECTARG1:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_SELECTARG1);
                            break;
                        case XTL::X_D3DTOP_SELECTARG2:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_SELECTARG2);
                            break;
                        case XTL::X_D3DTOP_MODULATE:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_MODULATE);
                            break;
                        case XTL::X_D3DTOP_MODULATE2X:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_MODULATE2X);
                            break;
                        case XTL::X_D3DTOP_MODULATE4X:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_MODULATE4X);
                            break;
                        case XTL::X_D3DTOP_ADD:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_ADD);
                            break;
                        case XTL::X_D3DTOP_ADDSIGNED:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_ADDSIGNED);
                            break;
                        case XTL::X_D3DTOP_ADDSIGNED2X:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_ADDSIGNED2X);
                            break;
                        case XTL::X_D3DTOP_SUBTRACT:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_SUBTRACT);
                            break;
                        case XTL::X_D3DTOP_ADDSMOOTH:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_ADDSMOOTH);
                            break;
                        case XTL::X_D3DTOP_BLENDDIFFUSEALPHA:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_BLENDDIFFUSEALPHA);
                            break;
                        case XTL::X_D3DTOP_BLENDCURRENTALPHA:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_BLENDCURRENTALPHA);
                            break;
                        case XTL::X_D3DTOP_BLENDTEXTUREALPHA:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_BLENDTEXTUREALPHA);
                            break;
                        case XTL::X_D3DTOP_BLENDFACTORALPHA:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_BLENDFACTORALPHA);
                            break;
                        case XTL::X_D3DTOP_BLENDTEXTUREALPHAPM:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_BLENDTEXTUREALPHAPM);
                            break;
                        case XTL::X_D3DTOP_PREMODULATE:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_PREMODULATE);
                            break;
                        case XTL::X_D3DTOP_MODULATEALPHA_ADDCOLOR:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_MODULATEALPHA_ADDCOLOR);
                            break;
                        case XTL::X_D3DTOP_MODULATECOLOR_ADDALPHA:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_MODULATECOLOR_ADDALPHA);
                            break;
                        case XTL::X_D3DTOP_MODULATEINVALPHA_ADDCOLOR:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_MODULATEINVALPHA_ADDCOLOR);
                            break;
                        case XTL::X_D3DTOP_MODULATEINVCOLOR_ADDALPHA:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_MODULATEINVCOLOR_ADDALPHA);
                            break;
                        case XTL::X_D3DTOP_DOTPRODUCT3:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_DOTPRODUCT3);
                            break;
                        case XTL::X_D3DTOP_MULTIPLYADD:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_MULTIPLYADD);
                            break;
                        case XTL::X_D3DTOP_LERP:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_LERP);
                            break;
                        case XTL::X_D3DTOP_BUMPENVMAP:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_BUMPENVMAP);
                            break;
                        case XTL::X_D3DTOP_BUMPENVMAPLUMINANCE:
                            g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLOROP, XTL::D3DTOP_BUMPENVMAPLUMINANCE);
                            break;
                        default:
                            EmuLog(LOG_LEVEL::WARNING, "Unsupported D3DTSS_COLOROP Value (%d)", Value);
                            break;
                        }
                    break;
                case XTL::X_D3DTSS_COLORARG0:
                    g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLORARG0, Value);
                    break;
                case XTL::X_D3DTSS_COLORARG1:
                    g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLORARG1, Value);
                    break;
                case XTL::X_D3DTSS_COLORARG2:
                    g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_COLORARG2, Value);
                    break;
                case XTL::X_D3DTSS_ALPHAOP:
                    // TODO: Use a lookup table, this is not always a 1:1 map (same as D3DTSS_COLOROP)
                    if (Value != X_D3DTSS_UNK) {
                        switch (Value) {
                            case XTL::X_D3DTOP_DISABLE:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_DISABLE);
                                break;
                            case XTL::X_D3DTOP_SELECTARG1:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_SELECTARG1);
                                break;
                            case XTL::X_D3DTOP_SELECTARG2:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_SELECTARG2);
                                break;
                            case XTL::X_D3DTOP_MODULATE:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_MODULATE);
                                break;
                            case XTL::X_D3DTOP_MODULATE2X:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_MODULATE2X);
                                break;
                            case XTL::X_D3DTOP_MODULATE4X:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_MODULATE4X);
                                break;
                            case XTL::X_D3DTOP_ADD:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_ADD);
                                break;
                            case XTL::X_D3DTOP_ADDSIGNED:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_ADDSIGNED);
                                break;
                            case XTL::X_D3DTOP_ADDSIGNED2X:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_ADDSIGNED2X);
                                break;
                            case XTL::X_D3DTOP_SUBTRACT:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_SUBTRACT);
                                break;
                            case XTL::X_D3DTOP_ADDSMOOTH:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_ADDSMOOTH);
                                break;
                            case XTL::X_D3DTOP_BLENDDIFFUSEALPHA:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_BLENDDIFFUSEALPHA);
                                break;
                            case XTL::X_D3DTOP_BLENDTEXTUREALPHA:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_BLENDTEXTUREALPHA);
                                break;
                            case XTL::X_D3DTOP_BLENDFACTORALPHA:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_BLENDFACTORALPHA);
                                break;
                            case XTL::X_D3DTOP_BLENDTEXTUREALPHAPM:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_BLENDTEXTUREALPHAPM);
                                break;
                            case XTL::X_D3DTOP_BLENDCURRENTALPHA:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_BLENDCURRENTALPHA);
                                break;
                            case XTL::X_D3DTOP_PREMODULATE:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_PREMODULATE);
                                break;
                            case XTL::X_D3DTOP_MODULATEALPHA_ADDCOLOR:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_MODULATEALPHA_ADDCOLOR);
                                break;
                            case XTL::X_D3DTOP_MODULATECOLOR_ADDALPHA:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_MODULATECOLOR_ADDALPHA);
                                break;
                            case XTL::X_D3DTOP_MODULATEINVALPHA_ADDCOLOR:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_MODULATEINVALPHA_ADDCOLOR);
                                break;
                            case XTL::X_D3DTOP_MODULATEINVCOLOR_ADDALPHA:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_MODULATEINVCOLOR_ADDALPHA);
                                break;
                            case XTL::X_D3DTOP_DOTPRODUCT3:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_DOTPRODUCT3);
                                break;
                            case XTL::X_D3DTOP_MULTIPLYADD:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_MULTIPLYADD);
                                break;
                            case XTL::X_D3DTOP_LERP:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_LERP);
                                break;
                            case XTL::X_D3DTOP_BUMPENVMAP:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_BUMPENVMAP);
                                break;
                            case XTL::X_D3DTOP_BUMPENVMAPLUMINANCE:
                                g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_BUMPENVMAPLUMINANCE);
                                break;
                            default:
                                EmuLog(LOG_LEVEL::WARNING, "Unsupported D3DTSS_ALPHAOP Value (%d)", Value);
                                break;
                      }
                    }
                    break;
                case XTL::X_D3DTSS_ALPHAARG0:
                    g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAARG0, Value);
                    break;
                case XTL::X_D3DTSS_ALPHAARG1:
                    g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAARG1, Value);
                    break;
                case XTL::X_D3DTSS_ALPHAARG2:
                    g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_ALPHAARG2, Value);
                    break;
                case XTL::X_D3DTSS_RESULTARG:
                    g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_RESULTARG, Value);
                    break;
                case XTL::X_D3DTSS_TEXTURETRANSFORMFLAGS:
                    g_pD3DDevice->SetTextureStageState(Stage, XTL::D3DTSS_TEXTURETRANSFORMFLAGS, Value);
                    break;
                default:
                    EmuLog(LOG_LEVEL::WARNING, "Unkown Xbox D3DTSS Value: %d", State);
                    break;
            }
        }
    }

    if (XTL::EmuD3DDeferredRenderState[XTL::X_D3DRS_POINTSPRITEENABLE - XTL::X_D3DRS_FOGENABLE] == TRUE) {
        XTL::IDirect3DBaseTexture *pTexture;

        // set the point sprites texture
        g_pD3DDevice->GetTexture(3, &pTexture);
        g_pD3DDevice->SetTexture(0, pTexture);

        // disable all other stages
        g_pD3DDevice->SetTextureStageState(1, XTL::D3DTSS_COLOROP, XTL::D3DTOP_DISABLE);
        g_pD3DDevice->SetTextureStageState(1, XTL::D3DTSS_ALPHAOP, XTL::D3DTOP_DISABLE);

        // no need to actually copy here, since it was handled in the loop above
    }
}

// ******************************************************************
// * patch: UpdateDeferredStates
// ******************************************************************
void XTL::EmuUpdateDeferredStates()
{
    VerifyAndFixEmuDeferredRenderStateOffset();
    UpdateDeferredRenderStates();
    UpdateDeferredTextureStates();
}

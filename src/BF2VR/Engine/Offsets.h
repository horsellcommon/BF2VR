// Copyright Ethan Porcaro

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

#pragma once

#include <Windows.h>
inline DWORD64 GameAddress(DWORD64 preferred) { return reinterpret_cast<DWORD64>(GetModuleHandleW(nullptr)) + preferred - 0x140000000ULL; }

///////////////////////////////////
// Class offsets
///////////////////////////////////

static const DWORD64 OFFSETGAMECONTEXT = GameAddress(0x143DD7948);
static const DWORD64 OFFSETLOCALAIMER = GameAddress(0x14406E610);

static const DWORD64 OFFSETWORLDRENDERSETTINGS = GameAddress(0x143D7B068);
static const DWORD64 OFFSETDXRENDERER = GameAddress(0x143FFBA28);
static const DWORD64 OFFSETGAMERENDERER = GameAddress(0x143ffbe10);
static const DWORD64 OFFSETSKYBOX = GameAddress(0x143F08260);

static const DWORD64 OFFSETUISETTINGS = GameAddress(0x143aebb80);

///////////////////////////////////
// Function offsets
///////////////////////////////////

static const DWORD64 OFFSETCAMERA = GameAddress(0x146FD3E90);
static const DWORD64 OFFSETEARLYVIEW = GameAddress(0x147C4DFEE);
static const DWORD64 OFFSETMAKEPROJECTION = GameAddress(0x147C3BE10);
static const DWORD64 OFFSETBUILDVIEWS = GameAddress(0x147c4e1a4);
static const DWORD64 OFFSETRESIZESCREEN = GameAddress(0x147d94769);
static const DWORD64 OFFSETPOSE = GameAddress(0x142150910);
// This is the instruction after a call to an address that dispatches render commands across the game. This specific call is for the UI
static const DWORD64 OFFSETUIDRAW = GameAddress(0x140e23e28);
static const DWORD64 OFFSETGAMEPADUPDATE = GameAddress(0x14774d402);
static const DWORD64 OFFSETPOST = GameAddress(0x14c659a00);

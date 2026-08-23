#pragma once

// メモリシステムの集約ヘッダ
// 利用側はこのヘッダ（またはPch.h）だけをインクルードすればよい

#include "Core\Memory\MemoryTypes.h"
#include "Core\Memory\MemoryUtility.h"
#include "Core\Memory\IAllocator.h"
#include "Core\Memory\LinearAllocator.h"
#include "Core\Memory\StackAllocator.h"
#include "Core\Memory\PoolAllocator.h"
#include "Core\Memory\FreeListAllocator.h"
#include "Core\Memory\MemoryTracker.h"
#include "Core\Memory\MemoryManager.h"
#include "Core\Memory\MemoryApi.h"
#include "Core\Memory\StlAllocator.h"

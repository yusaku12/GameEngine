#pragma once

// エンジン基盤機能の集約ヘッダ
// 明示的に利用するための集約ヘッダ。通常は必要な機能のヘッダだけをインクルードする

#include "Core\CoreDefines.h"
#include "Core\Logging\Logging.h"
#include "Core\Containers\Containers.h"
#include "Core\Containers\BitFlags.h"
#include "Core\Math\Math.h"
#include "Core\Threading\Threading.h"
#include "Core\Time\HighResolutionTimer.h"
#include "Core\Time\TimeManager.h"
#include "Core\Input\InputManager.h"
#include "Core\GameObject\Component.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"
#include "Core\GameObject\GameObjectManager.h"
#include "Core\Scene\Scene.h"
#include "Core\Scene\SceneManager.h"
#include "Core\Scene\TagManager.h"
#include "Core\Scene\LayerManager.h"
#include "Core\Scene\SceneSerializer.h"
#include "Core\Prefab\Prefab.h"
#include "Core\Prefab\PrefabInstance.h"
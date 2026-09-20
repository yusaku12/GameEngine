#pragma once

#include <cstdint>
#include "Assets\Material\MaterialTypes.h"

namespace Engine
{
    /**
     * @brief SkeletonSignature は Skeleton の構造を一意に識別するための署名。
     * @details Skeleton のボーン構造が変更された場合、Signature も変更される。
     */
    struct SkeletonSignature
    {
        std::uint64_t high = 0; //!< 上位 64 ビットの署名
        std::uint64_t low = 0;  //!<下位 64 ビットの署名

        /**
         * @brief SkeletonSignature が有効かを判定する
         * @return 有効な場合は true
         */
        bool isValid() const noexcept { return high != 0 || low != 0; }
        friend bool operator==(const SkeletonSignature&, const SkeletonSignature&) = default;
    };

    /**
     * @brief SkeletonHandle は Skeleton を一意に識別するためのハンドル。
     * @details Skeleton の生成と破棄を追跡するために、インデックスと世代番号を組み合わせて使用する。
     */
    struct SkeletonHandle
    {
        //! SkeletonHandle の無効なインデックス値
        static constexpr std::uint32_t INVALID_INDEX = UINT32_MAX;

        std::uint32_t index = INVALID_INDEX; //!< Skeleton のインデックス
        std::uint32_t generation = 0;        //!< Skeleton の世代番号。Skeleton が破棄されると generation は増加する。

        /**
         * @brief SkeletonHandle が有効かを判定する
         * @return 有効な場合は true
         */
        bool isValid() const noexcept { return index != INVALID_INDEX && generation != 0; }
        static constexpr SkeletonHandle Invalid() noexcept { return {}; }
        friend bool operator==(const SkeletonHandle&, const SkeletonHandle&) = default;
    };

    /**
     * @brief AnimationClipHandle は AnimationClip を一意に識別するためのハンドル。
     * @details AnimationClip の生成と破棄を追跡するために、インデックスと世代番号を組み合わせて使用する。
     */
    struct AnimationClipHandle
    {
        //! AnimationClipHandle の無効なインデックス値
        static constexpr std::uint32_t INVALID_INDEX = UINT32_MAX;

        std::uint32_t index = INVALID_INDEX; //!< AnimationClip のインデックス
        std::uint32_t generation = 0;        //!< AnimationClip の世代番号。AnimationClip が破棄されると generation は増加する。

        /**
         * @brief AnimationClipHandle が有効かを判定する
         * @return 有効な場合は true
         */
        bool isValid() const noexcept { return index != INVALID_INDEX && generation != 0; }
        static constexpr AnimationClipHandle Invalid() noexcept { return {}; }
        friend bool operator==(const AnimationClipHandle&, const AnimationClipHandle&) = default;
    };

    /**
     * @brief AnimatorControllerを一意に識別する世代付きハンドル。
     */
    struct AnimatorControllerHandle
    {
        //! AnimatorControllerHandle の無効なインデックス値
        static constexpr std::uint32_t INVALID_INDEX = UINT32_MAX;

        std::uint32_t index = INVALID_INDEX; //!< AnimatorController のインデックス
        std::uint32_t generation = 0;        //!< AnimatorController の世代番号。AnimatorController が破棄されると generation は増加する。

        /**
         * @brief AnimatorControllerHandle が有効かを判定する
         * @return 有効な場合は true
         */
        bool isValid() const noexcept { return index != INVALID_INDEX && generation != 0; }
        static constexpr AnimatorControllerHandle Invalid() noexcept { return {}; }
        friend bool operator==(const AnimatorControllerHandle&, const AnimatorControllerHandle&) = default;
    };

    /**
     * @brief AnimationWrapMode はアニメーションの再生方法を指定する列挙型。
     */
    enum class AnimationWrapMode : std::uint8_t
    {
        Once,     //!< アニメーションを一度だけ再生する
        Loop,     //!< アニメーションをループ再生する
        PingPong, //!< アニメーションを前後に往復して再生する
    };
}

#pragma once

#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/skeleton.h>

namespace Engine::Serialization
{
    /**
     * @brief Ozz ArchiveのChecksumを計算する。
     * @param bytes Ozz Archiveのバイト列
     * @return 計算されたChecksum値
     */
    std::uint64_t calculateArchiveChecksum(std::span<const std::uint8_t> bytes) noexcept;

    /**
     * @brief Ozz Archiveをバイト列に保存する。
     * @param skeleton 保存するSkeleton
     * @param bytes 保存先のバイト列
     * @return 保存に成功した場合はtrue
     */
    bool saveOzzArchive(const ozz::animation::Skeleton& skeleton, std::vector<std::uint8_t>& bytes);

    /**
     * @brief Ozz Archiveをバイト列に保存する。
     * @param animation 保存するAnimation
     * @param bytes 保存先のバイト列
     * @return 保存に成功した場合はtrue
     */
    bool saveOzzArchive(const ozz::animation::Animation& animation, std::vector<std::uint8_t>& bytes);

    /**
     * @brief Ozz Archiveをバイト列から読み込む。
     * @param bytes 読み込むOzz Archiveのバイト列
     * @param skeleton 読み込んだSkeletonの出力先
     * @return 読み込みに成功した場合はtrue
     */
    bool loadOzzArchive(std::span<const std::uint8_t> bytes, ozz::animation::Skeleton& skeleton);

    /**
     * @brief Ozz Archiveをバイト列から読み込む。
     * @param bytes 読み込むOzz Archiveのバイト列
     * @param animation 読み込んだAnimationの出力先
     * @return 読み込みに成功した場合はtrue
     */
    bool loadOzzArchive(std::span<const std::uint8_t> bytes, ozz::animation::Animation& animation);
}

#include "Pch.h"

#include <cctype>

#include "Core\Containers\String.h"

namespace Engine
{

String::String()
    : m_data(m_inline)
    , m_size(0)
    , m_capacity(INLINE_CAPACITY)
    , m_inline{}
{
}

String::String(const char* text)
    : String()
{
    assign(text, textLength(text));
}

String::String(const char* text, size_t size)
    : String()
{
    assign(text, size);
}

String::String(std::string_view text)
    : String()
{
    assign(text.data(), text.size());
}

String::String(const String& other)
    : String()
{
    assign(other.m_data, other.m_size);
}

String::String(String&& other) noexcept
    : String()
{
    if (other.isInline())
    {
        std::memcpy(m_inline, other.m_inline, other.m_size + 1);
        m_size = other.m_size;
    }
    else
    {
        m_data = other.m_data;
        m_size = other.m_size;
        m_capacity = other.m_capacity;
    }

    other.m_data = other.m_inline;
    other.m_size = 0;
    other.m_capacity = INLINE_CAPACITY;
    other.m_inline[0] = '\0';
}

String::~String()
{
    releaseStorage();
}

String& String::operator=(const String& other)
{
    if (this != &other)
        assign(other.m_data, other.m_size);

    return *this;
}

String& String::operator=(String&& other) noexcept
{
    if (this == &other)
        return *this;

    releaseStorage();

    if (other.isInline())
    {
        m_data = m_inline;
        m_capacity = INLINE_CAPACITY;
        std::memcpy(m_inline, other.m_inline, other.m_size + 1);
    }
    else
    {
        m_data = other.m_data;
        m_capacity = other.m_capacity;
    }

    m_size = other.m_size;

    other.m_data = other.m_inline;
    other.m_size = 0;
    other.m_capacity = INLINE_CAPACITY;
    other.m_inline[0] = '\0';

    return *this;
}

String& String::operator=(const char* text)
{
    assign(text, textLength(text));
    return *this;
}

String String::fromWide(const wchar_t* text)
{
    if (text == nullptr || text[0] == L'\0')
        return String();

    const int length = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1)
        return String();

    String result;
    result.resize(static_cast<size_t>(length) - 1);
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);

    return result;
}

std::wstring String::toWide() const
{
    if (m_size == 0)
        return std::wstring();

    const int length = ::MultiByteToWideChar(CP_UTF8, 0, m_data, static_cast<int>(m_size), nullptr, 0);
    if (length <= 0)
        return std::wstring();

    std::wstring result(static_cast<size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, m_data, static_cast<int>(m_size), result.data(), length);

    return result;
}

void String::reserve(size_t capacity)
{
    if (capacity <= m_capacity)
        return;

    // 拡張のたびに確保しないよう、少なくとも2倍に伸ばす
    size_t newCapacity = m_capacity * 2;
    if (newCapacity < capacity)
        newCapacity = capacity;

    memorySetSource(nullptr, 0, MemoryTag::CONTAINER);
    void* memory = memoryAllocate(newCapacity + 1, alignof(char));
    GE_ASSERT_MSG(memory != nullptr, "文字列の確保に失敗しました ({} 文字)", newCapacity);

    if (memory == nullptr)
        return;

    char* newData = static_cast<char*>(memory);
    std::memcpy(newData, m_data, m_size + 1);

    releaseStorage();

    m_data = newData;
    m_capacity = newCapacity;
}

void String::resize(size_t size, char fill)
{
    reserve(size);

    if (size > m_size)
        std::memset(m_data + m_size, fill, size - m_size);

    m_size = size;
    m_data[m_size] = '\0';
}

void String::clear()
{
    m_size = 0;
    m_data[0] = '\0';
}

void String::append(const char* text, size_t size)
{
    if (text == nullptr || size == 0)
        return;

    reserve(m_size + size);

    std::memcpy(m_data + m_size, text, size);
    m_size += size;
    m_data[m_size] = '\0';
}

String String::substring(size_t offset, size_t count) const
{
    if (offset >= m_size)
        return String();

    const size_t available = m_size - offset;
    return String(m_data + offset, count < available ? count : available);
}

size_t String::find(std::string_view text, size_t offset) const
{
    const size_t result = view().find(text, offset);
    return result == std::string_view::npos ? NPOS : result;
}

size_t String::find(char character, size_t offset) const
{
    const size_t result = view().find(character, offset);
    return result == std::string_view::npos ? NPOS : result;
}

bool String::startsWith(std::string_view text) const
{
    return view().starts_with(text);
}

bool String::endsWith(std::string_view text) const
{
    return view().ends_with(text);
}

String String::trimmed() const
{
    size_t first = 0;
    while (first < m_size && std::isspace(static_cast<unsigned char>(m_data[first])) != 0)
        ++first;

    size_t last = m_size;
    while (last > first && std::isspace(static_cast<unsigned char>(m_data[last - 1])) != 0)
        --last;

    return String(m_data + first, last - first);
}

String String::toUpper() const
{
    String result(*this);
    for (size_t index = 0; index < result.m_size; ++index)
        result.m_data[index] = static_cast<char>(std::toupper(static_cast<unsigned char>(result.m_data[index])));

    return result;
}

String String::toLower() const
{
    String result(*this);
    for (size_t index = 0; index < result.m_size; ++index)
        result.m_data[index] = static_cast<char>(std::tolower(static_cast<unsigned char>(result.m_data[index])));

    return result;
}

void String::assign(const char* text, size_t size)
{
    if (text == nullptr)
        size = 0;

    reserve(size);

    if (size > 0)
        std::memmove(m_data, text, size);

    m_size = size;
    m_data[m_size] = '\0';
}

void String::releaseStorage()
{
    if (!isInline())
        memoryFree(m_data);

    m_data = m_inline;
    m_capacity = INLINE_CAPACITY;
}

} // namespace Engine

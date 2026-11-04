#include "Pch.h"
#include "Core\Containers\String.h"
#include "Core\Logging\Logger.h"

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
            m_storage = std::move(other.m_storage);
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
        }

        other.m_data = other.m_inline;
        other.m_size = 0;
        other.m_capacity = INLINE_CAPACITY;
        other.m_inline[0] = '\0';
    }

    String::~String() = default;

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
            m_storage = std::move(other.m_storage);
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

        const int length = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
        if (length == 0)
        {
            LOG_ERROR("[String] Could not measure UTF-8 conversion (Windows error: {}).", GetLastError());
            return String();
        }
        if (length == 1)
            return String();

        String result;
        result.resize(static_cast<size_t>(length) - 1);
        if (result.size() != static_cast<size_t>(length) - 1)
            return String();
        if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, result.data(), length, nullptr, nullptr) != length)
        {
            LOG_ERROR("[String] UTF-8 conversion failed (Windows error: {}).", GetLastError());
            return String();
        }

        return result;
    }

    std::wstring String::toWide() const
    {
        if (m_size == 0)
            return std::wstring();

        if (m_size > static_cast<size_t>((std::numeric_limits<int>::max)()))
        {
            LOG_ERROR("[String] UTF-8 text exceeds the supported conversion length.");
            return std::wstring();
        }

        const int sourceLength = static_cast<int>(m_size);
        const int length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            m_data, sourceLength, nullptr, 0);
        if (length <= 0)
        {
            LOG_ERROR("[String] UTF-16 conversion failed (Windows error: {}).", GetLastError());
            return std::wstring();
        }

        std::wstring result(static_cast<size_t>(length), L'\0');
        if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            m_data, sourceLength, result.data(), length) != length)
        {
            LOG_ERROR("[String] UTF-16 conversion failed (Windows error: {}).", GetLastError());
            return std::wstring();
        }

        return result;
    }

    void String::reserve(size_t capacity)
    {
        if (capacity <= m_capacity)
            return;

        constexpr size_t maxCapacity = (std::numeric_limits<size_t>::max)() - 1;
        if (capacity > maxCapacity)
        {
            LOG_ERROR("[String] Requested capacity exceeds the supported limit.");
            return;
        }

        // 拡張のたびに確保しないよう、少なくとも2倍に伸ばす
        size_t newCapacity = m_capacity <= maxCapacity / 2 ? m_capacity * 2 : m_capacity;
        if (newCapacity < capacity)
            newCapacity = capacity;

        std::unique_ptr<char[]> newData(new (std::nothrow) char[newCapacity + 1]);
        if (newData == nullptr)
        {
            LOG_ERROR("[String] Could not allocate storage for {} characters.", newCapacity);
            return;
        }
        std::memcpy(newData.get(), m_data, m_size + 1);

        m_storage = std::move(newData);
        m_data = m_storage.get();
        m_capacity = newCapacity;
    }

    void String::resize(size_t size, char fill)
    {
        reserve(size);
        if (size > m_capacity)
            return;

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
        if (!validateSource(text, size) || size == 0)
            return;

        constexpr size_t maxCapacity = (std::numeric_limits<size_t>::max)() - 1;
        if (size > maxCapacity - m_size)
        {
            LOG_ERROR("[String] Appended text exceeds the supported capacity.");
            return;
        }

        const std::uintptr_t dataAddress = reinterpret_cast<std::uintptr_t>(m_data);
        const std::uintptr_t textAddress = reinterpret_cast<std::uintptr_t>(text);
        const bool aliasesStorage = textAddress >= dataAddress
            && textAddress - dataAddress <= m_size;
        const size_t sourceOffset = aliasesStorage
            ? static_cast<size_t>(textAddress - dataAddress)
            : 0;
        const size_t newSize = m_size + size;
        reserve(newSize);
        if (newSize > m_capacity)
            return;

        if (aliasesStorage)
            text = m_data + sourceOffset;
        std::memmove(m_data + m_size, text, size);
        m_size = newSize;
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
        if (!validateSource(text, size))
            return;

        reserve(size);
        if (size > m_capacity)
            return;

        if (size > 0)
            std::memmove(m_data, text, size);

        m_size = size;
        m_data[m_size] = '\0';
    }

    bool String::validateSource(const char* text, const size_t size) const
    {
        if (text == nullptr)
        {
            if (size == 0)
                return true;
            LOG_ERROR("[String] A nonempty source requires a valid pointer.");
            return false;
        }
        const auto dataAddress = reinterpret_cast<std::uintptr_t>(m_data);
        const auto textAddress = reinterpret_cast<std::uintptr_t>(text);
        if (textAddress >= dataAddress && textAddress - dataAddress <= m_capacity)
        {
            const size_t offset = static_cast<size_t>(textAddress - dataAddress);
            if (offset > m_size || size > m_size - offset)
            {
                LOG_ERROR("[String] Source range exceeds the current string.");
                return false;
            }
        }
        return true;
    }

    void String::releaseStorage()
    {
        m_storage.reset();

        m_data = m_inline;
        m_capacity = INLINE_CAPACITY;
    }
} // namespace Engine
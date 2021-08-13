/***************************************************************************

    utility.h

    Miscellaneous utility code

***************************************************************************/

#pragma once

#ifndef UTILITY_H
#define UTILITY_H

#include <unordered_map>
#include <optional>
#include <functional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <wctype.h>

#include <QFileInfo>
#include <QWidget>


//**************************************************************************
//  QT VERSION CHECK
//**************************************************************************

// we use QTreeView::expandRecursively(), which was introduced in Qt 5.13
#if QT_VERSION < QT_VERSION_CHECK(5, 13, 0)
#error BletchMAME requires Qt version 5.13
#endif


//**************************************************************************
//  HASHES AND EQUIVALENCY
//**************************************************************************

namespace util
{
	template<class TStr>
	std::size_t string_hash(const TStr *s, std::size_t length)
	{
		std::size_t result = 31337;
		for (std::size_t i = 0; i < length; i++)
			result = ((result << 5) + result) + s[i];
		return result;
	}
};


namespace std
{
	template<> class hash<const char *>
	{
	public:
		std::size_t operator()(const char *s) const
		{
			return util::string_hash(s, strlen(s));
		}
	};

	template<> class equal_to<const char *>
	{
	public:
		bool operator()(const char *s1, const char *s2) const
		{
			return !strcmp(s1, s2);
		}
	};
}



namespace util {


//**************************************************************************
//  PARSING UTILITY CLASSES
//**************************************************************************

template<typename TFunc, typename TValue, TValue Value>
class return_value_substitutor
{
public:
	return_value_substitutor(TFunc &&func)
		: m_func(std::move(func))
	{
	}

	template<typename... TArgs>
	TValue operator()(TArgs&&... args)
	{
		m_func(args...);
		return Value;
	}

private:
	TFunc m_func;
};


template<typename TIterator, typename TPredicate>
auto find_if_ptr(TIterator first, TIterator last, TPredicate predicate)
{
	auto iter = std::find_if(first, last, predicate);
	return iter != last
		? &*iter
		: nullptr;
}


template<typename TContainer, typename TPredicate>
auto find_if_ptr(TContainer &container, TPredicate predicate)
{
	return find_if_ptr(container.begin(), container.end(), predicate);
}


template<typename TContainer>
bool contains(typename TContainer::const_iterator begin, typename TContainer::const_iterator end, const typename TContainer::value_type &value)
{
	return std::find(begin, end, value) != end;
}


template<typename TContainer>
bool contains(const TContainer &container, const typename TContainer::value_type &value)
{
	return contains<TContainer>(container.cbegin(), container.cend(), value);
}


template<typename TContainer, typename TPredicate>
bool contains_if(typename TContainer::const_iterator begin, typename TContainer::const_iterator end, TPredicate predicate)
{
	return std::find_if(begin, end, predicate) != end;
}


template<typename TContainer, typename TPredicate>
bool contains_if(const TContainer &container, TPredicate predicate)
{
	return contains_if<TContainer>(container.cbegin(), container.cend(), predicate);
}


//**************************************************************************
//  ENUM UTILITY CLASSES
//**************************************************************************

// ======================> enum_parser
template<typename T>
class enum_parser
{
public:
	enum_parser(std::initializer_list<std::pair<const char *, T>> values)
		: m_map(values.begin(), values.end())
	{
	}

	bool operator()(const std::string &text, T &value) const
	{
		auto iter = m_map.find(text.c_str());
		bool success = iter != m_map.end();
		value = success ? iter->second : T();
		return success;
	}

	bool operator()(const std::string &text, std::optional<T> &value) const
	{
		T inner_value;
		bool success = (*this)(text, inner_value);
		value = success ? inner_value : std::optional<T>();
		return success;
	}

private:
	const std::unordered_map<const char *, T> m_map;
};


// ======================> enum_parser_bidirectional
template<typename T>
class enum_parser_bidirectional : public enum_parser<T>
{
public:
	enum_parser_bidirectional(std::initializer_list<std::pair<const char *, T>> values)
		: enum_parser<T>(values)
	{
		for (const auto &[str, value] : values)
			m_reverse_map.emplace(value, str);
	}

	const char *operator[](T val) const
	{
		auto iter = m_reverse_map.find(val);
		return iter->second;
	}


private:
	std::unordered_map<T, const char *> m_reverse_map;
};


// ======================> all_enums
template<typename T>
class all_enums
{
public:
	class iterator
	{
	public:
		iterator(T value) : m_value(value) { }
		T operator*() const { return m_value; }
		iterator &operator++() { bump(+1); return *this; }
		iterator &operator--() { bump(-1); return *this; }
		iterator operator++(int) { iterator result = *this; bump(+1); return result; }
		iterator operator--(int) { iterator result = *this; bump(-1); return result; }
		auto operator<=>(const iterator &that) const = default;

	private:
		T m_value;

		void bump(std::int64_t offset)
		{
			auto i = static_cast<std::int64_t>(m_value);
			i += offset;
			m_value = static_cast<T>(i);
		}
	};

	typedef iterator const_iterator;

	iterator begin()		const { return iterator((T)0); }
	iterator end()			const { return iterator(T::COUNT); }
	const_iterator cbegin()	const { return begin(); }
	const_iterator cend()	const { return end(); }
};


//**************************************************************************
//  STRING & CONTAINER UTILITIES
//**************************************************************************

extern const QString g_empty_string;


//-------------------------------------------------
//  string_split
//-------------------------------------------------

template<typename TStr, typename TFunc>
std::vector<TStr> string_split(const TStr &str, TFunc &&is_delim)
{
	std::vector<TStr> results;

	auto word_begin = str.cbegin();
	for (auto iter = str.cbegin(); iter < str.cend(); iter++)
	{
		if (is_delim(*iter))
		{
			if (word_begin < iter)
			{
				TStr word(&*word_begin, iter - word_begin);
				results.emplace_back(std::move(word));
			}

			word_begin = iter;
			word_begin++;
		}
	}

	// squeeze off that final word, if necessary
	if (word_begin < str.cend())
	{
		TStr word(&*word_begin, str.cend() - word_begin);
		results.emplace_back(std::move(word));
	}

	return results;
}


//-------------------------------------------------
//  string_join
//-------------------------------------------------

template<typename TStr, typename TColl, typename TFunc>
TStr string_join(const TStr &delim, const TColl &collection, TFunc func)
{
	TStr result;
	bool is_first = true;

	for (const TStr &member : collection)
	{
		if (is_first)
			is_first = false;
		else
			result += delim;
		result += func(member);
	}
	return result;
}


template<typename TStr, typename TColl>
TStr string_join(const TStr &delim, const TColl &collection)
{
	return string_join(delim, collection, [](const TStr &s) { return s; });
}


//-------------------------------------------------
//  last
//-------------------------------------------------

template<typename T>
auto &last(T &container)
{
	assert(container.end() > container.begin());
	return *(container.end() - 1);
}


//-------------------------------------------------
//  salt
//-------------------------------------------------

inline void salt(void *destination, const void *original, size_t original_size, const void *salt, size_t salt_size)
{
	char *destination_ptr = (char *)destination;
	const char *original_ptr = (char *)original;
	const char *salt_ptr = (char *)salt;

	for (size_t i = 0; i < original_size; i++)
		destination_ptr[i] = original_ptr[i] ^ salt_ptr[i % salt_size];
}


template<typename TStruct, typename TSalt>
TStruct salt(const TStruct &original, const TSalt &salt_value)
{
	TStruct result;
	salt((void *)&result, (const void *)&original, sizeof(original), (const void *)&salt_value, sizeof(salt_value));
	return result;
}


//-------------------------------------------------
//  to_utf8_string
//-------------------------------------------------

inline std::string to_utf8_string(const QString &str)
{
	auto byte_array = str.toUtf8();
	return std::string(byte_array.constData(), byte_array.size());
}


//-------------------------------------------------
//  safe_static_cast
//-------------------------------------------------

template<class T> T safe_static_cast(size_t sz)
{
	auto result = static_cast<T>(sz);
	if (sz != result)
		throw std::overflow_error("Overflow");
	return result;
}


//-------------------------------------------------
//  binaryFromHex
//-------------------------------------------------

std::size_t binaryFromHex(std::span<uint8_t> &dest, std::string_view hex);

template<std::size_t N>
std::size_t binaryFromHex(uint8_t (&dest)[N], std::string_view hex)
{
	std::span<uint8_t> destSpan(dest);
	return binaryFromHex(destSpan, hex);
}


//**************************************************************************
//  COMMAND LINE
//**************************************************************************

QString build_command_line(const QString &executable, const std::vector<QString> &argv);


//**************************************************************************

}; // namespace util

//**************************************************************************
//  WXWIDGETS IMPERSONATION
//**************************************************************************

class wxFileName
{
public:
	static bool IsPathSeparator(QChar ch);
	static void SplitPath(const QString &fullpath, QString *path, QString *name, QString *ext);
};


// useful for popup menus
QPoint globalPositionBelowWidget(const QWidget &widget);

// remove extra items in a grid layout
class QGridLayout;
void truncateGridLayout(QGridLayout &gridLayout, int rows);


#endif // UTILITY_H

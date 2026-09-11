// Forced into every C++ translation unit (see CMakeLists.txt).
//
// Qt 6 removed a handful of free functions that HP's code calls, and its
// headers include less than Qt 5's did. Everything here either brings a removed
// function back on top of its Qt 6 replacement, or restores an include the code
// relied on getting for free.

#ifndef QT6COMPAT_H
#define QT6COMPAT_H

#ifdef __cplusplus

#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)

#include <algorithm>
#include <cstdlib>

// Qt 5's QtGui headers pulled QObject in, and several of HP's headers derive
// from it without including it: "invalid use of incomplete type 'class QObject'".
#include <QObject>
#include <QRandomGenerator>
#include <QVariant>
// Also pulled in by Qt 5's headers and used without an include by WebAppMgr.
#include <QFile>
#include <QTimer>
#include <QUrlQuery>

// qrand() and qsrand(): per thread in Qt 5, and an unseeded qrand() behaved as
// if seeded with 1. A thread-local generator keeps both; QRandomGenerator's
// global() cannot be seeded at all.
inline QRandomGenerator& qt6compatRandom()
{
    thread_local QRandomGenerator generator(1);
    return generator;
}

inline void qsrand(uint seed)
{
    qt6compatRandom().seed(seed);
}

// Same range as Qt 5: [0, RAND_MAX].
inline int qrand()
{
    return int(qt6compatRandom().generate() & RAND_MAX);
}

// qSort() and qFind(), from QtAlgorithms, which Qt 6 dropped for <algorithm>.
template <typename Iterator>
inline void qSort(Iterator begin, Iterator end)
{
    std::sort(begin, end);
}

template <typename Iterator, typename LessThan>
inline void qSort(Iterator begin, Iterator end, LessThan lessThan)
{
    std::sort(begin, end, lessThan);
}

template <typename Container>
inline void qSort(Container& container)
{
    std::sort(container.begin(), container.end());
}

template <typename Iterator, typename T>
inline Iterator qFind(Iterator begin, Iterator end, const T& value)
{
    return std::find(begin, end, value);
}

template <typename Container, typename T>
inline typename Container::const_iterator qFind(const Container& container, const T& value)
{
    return std::find(container.constBegin(), container.constEnd(), value);
}

template <typename T>
inline QVariant qVariantFromValue(const T& value)
{
    return QVariant::fromValue(value);
}

// Qt 6 records a type's stream operators when the type itself is registered,
// so registering the type is all that is left to do.
template <typename T>
inline void qRegisterMetaTypeStreamOperators(const char* typeName)
{
    qRegisterMetaType<T>(typeName);
}

#endif // QT_VERSION >= 6
#endif // __cplusplus
#endif // QT6COMPAT_H

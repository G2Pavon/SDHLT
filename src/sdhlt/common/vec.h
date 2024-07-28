#pragma once

#include <cmath>
#include <limits>
#include <iostream>

template <typename T>
class Vector3
{
public:
    T x, y, z;

    // Constructores
    Vector3() : x(T(0)), y(T(0)), z(T(0)) {}
    Vector3(T x, T y, T z) : x(x), y(y), z(z) {}

    // Métodos de operadores
    Vector3<T> &operator+=(const Vector3<T> &other)
    {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    Vector3<T> &operator-=(const Vector3<T> &other)
    {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }

    Vector3<T> &operator*=(T scalar)
    {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    Vector3<T> &operator/=(T scalar)
    {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        return *this;
    }

    T dot(const Vector3<T> &other) const
    {
        return x * other.x + y * other.y + z * other.z;
    }

    Vector3<T> cross(const Vector3<T> &other) const
    {
        return Vector3<T>(
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x);
    }

    T length() const
    {
        return std::sqrt(x * x + y * y + z * z);
    }

    Vector3<T> normalized() const
    {
        T len = length();
        if (len < std::numeric_limits<T>::epsilon())
        {
            return Vector3<T>(T(0), T(0), T(0));
        }
        return Vector3<T>(x / len, y / len, z / len);
    }

    bool equals(const Vector3<T> &other, T epsilon = std::numeric_limits<T>::epsilon()) const
    {
        return std::fabs(x - other.x) <= epsilon &&
               std::fabs(y - other.y) <= epsilon &&
               std::fabs(z - other.z) <= epsilon;
    }

    friend std::ostream &operator<<(std::ostream &os, const Vector3<T> &vec)
    {
        os << "(" << vec.x << ", " << vec.y << ", " << vec.z << ")";
        return os;
    }

    friend std::istream &operator>>(std::istream &is, Vector3<T> &vec)
    {
        is >> vec.x >> vec.y >> vec.z;
        return is;
    }
};

template <typename T>
Vector3<T> operator+(Vector3<T> lhs, const Vector3<T> &rhs)
{
    lhs += rhs;
    return lhs;
}

template <typename T>
Vector3<T> operator-(Vector3<T> lhs, const Vector3<T> &rhs)
{
    lhs -= rhs;
    return lhs;
}

template <typename T>
Vector3<T> operator*(Vector3<T> vec, T scalar)
{
    vec *= scalar;
    return vec;
}

template <typename T>
Vector3<T> operator*(T scalar, Vector3<T> vec)
{
    vec *= scalar;
    return vec;
}

template <typename T>
Vector3<T> operator/(Vector3<T> vec, T scalar)
{
    vec /= scalar;
    return vec;
}
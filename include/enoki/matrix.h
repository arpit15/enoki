/*
    enoki/matrix.h -- Convenience wrapper for square matrixes

    Enoki is a C++ template library that enables transparent vectorization
    of numerical kernels using SIMD instruction sets available on current
    processor architectures.

    Copyright (c) 2017 Wenzel Jakob <wenzel.jakob@epfl.ch>

    All rights reserved. Use of this source code is governed by a BSD-style
    license that can be found in the LICENSE file.
*/

#pragma once

#include "array.h"

NAMESPACE_BEGIN(enoki)

/**
 * \brief Dense square matrix data structure of static size
 * \remark Uses column-major storage order to permit efficient vectorization
 */
template <typename Type_, size_t Size_>
struct Matrix
    : StaticArrayImpl<Array<Type_, Size_>, Size_,
                      detail::approx_default<Type_>::value,
                      RoundingMode::Default, Matrix<Type_, Size_>> {

    using Type = Type_;
    using Column = Array<Type_, Size_>;
    using Entry = value_t<Column>;

    using Base = StaticArrayImpl<Column, Size_,
                                 detail::approx_default<Type_>::value,
                                 RoundingMode::Default, Matrix<Type_, Size_>>;

    template <typename T> using ReplaceType = Matrix<T, Size_>;

    ENOKI_DECLARE_CUSTOM_ARRAY(Base, Matrix)

    using Base::coeff;
    using Base::Size;

    /// Initialize the matrix from a set of coefficients
    template <typename... Args, std::enable_if_t<
              detail::all_of<std::is_constructible<Entry, Args>::value...,
                             sizeof...(Args) == Size_ * Size_>::value, int> = 0>
    ENOKI_INLINE Matrix(const Args&... args) {
        alignas(alignof(Column)) Entry values[sizeof...(Args)] = { Entry(args)... };
        for (size_t j = 0; j < Size; ++j)
            for (size_t i = 0; i < Size; ++i)
                coeff(j).coeff(i) = values[i * Size + j];
    }

    Matrix(Type f) : Base(zero<Type>()) {
        for (size_t i = 0; i < Matrix::Size; ++i)
            operator()(i, i) = f;
    }

    template <typename T = Type,
              std::enable_if_t<!std::is_same<T, scalar_t<T>>::value, int> = 0>
    Matrix(scalar_t<T> f) : Base(zero<Type>()) {
        for (size_t i = 0; i < Matrix::Size; ++i)
            operator()(i, i) = f;
    }

    template <typename... Column>
    ENOKI_INLINE static Matrix from_cols(const Column&... cols) {
        return Matrix(cols...);
    }

    template <typename... Row>
    ENOKI_INLINE static Matrix from_rows(const Row&... rows) {
        return transpose(Matrix(rows...));
    }

    /// Return a reference to the (i, j) element
    ENOKI_INLINE Entry& operator()(size_t i, size_t j) { return coeff(j, i); }

    /// Return a reference to the (i, j) element (const)
    ENOKI_INLINE const Entry& operator()(size_t i, size_t j) const { return coeff(j, i); }

    ENOKI_ALIGNED_OPERATOR_NEW()
};

template <typename T0, typename T1, size_t Size,
          typename Scalar = decltype(std::declval<T0>() + std::declval<T1>()),
          typename Return = Matrix<expr_t<Scalar>, Size>,
          typename Column = typename Return::Column>
ENOKI_INLINE Return operator*(Matrix<T0, Size> m0, Matrix<T1, Size> m1) {
    Return result;
    /* 4x4 case reduced to 4 multiplications, 12 fused multiply-adds,
       and 16 broadcasts (also fused on AVX512VL) */
    for (size_t j = 0; j < Size; ++j) {
        Column sum = m0.coeff(0) * m1(0, j);
        for (size_t i = 1; i < Size; ++i)
            sum = fmadd(m0.coeff(i), Column(m1(i, j)), sum);
        result.coeff(j) = sum;
    }

    return result;
}

template <typename T0, typename T1, size_t Size, size_t Size2,
          bool Approx, RoundingMode Mode, typename Derived,
          std::enable_if_t<Size == Derived::Size, int> = 0,
          typename Type = decltype(std::declval<T0>() + std::declval<T1>()),
          typename Return = Array<Type, Size>>
ENOKI_INLINE Return
operator*(Matrix<T0, Size> m,
          const StaticArrayBase<T1, Size2, Approx, Mode, Derived> &v) {
    Return sum = m.coeff(0) * v.derived().coeff(0);
    for (size_t i = 1; i < Size; ++i)
        sum = fmadd(m.coeff(i), Return(v.derived().coeff(i)), sum);
    return sum;
}

template <typename T, size_t Size>
ENOKI_INLINE Matrix<expr_t<T>, Size> operator*(Matrix<T, Size> m, expr_t<T> s) {
    return Array<Array<expr_t<T>, Size>, Size>(m) * s;
}

template <typename T, size_t Size, std::enable_if_t<!std::is_same<expr_t<T>, scalar_t<T>>::value, int> = 0>
ENOKI_INLINE Matrix<expr_t<T>, Size> operator*(Matrix<T, Size> m, scalar_t<T> s) {
    return Array<Array<expr_t<T>, Size>, Size>(m) * s;
}

template <typename Type, size_t Size>
ENOKI_INLINE expr_t<Type> trace(Matrix<Type, Size> m) {
    expr_t<Type> result = m.coeff(0, 0);
    for (size_t i = 1; i < Size; ++i)
        result += m(i, i);
    return result;
}

template <typename Matrix> ENOKI_INLINE Matrix identity() {
    Matrix result = zero<Matrix>();
    for (size_t i = 0; i < Matrix::Size; ++i)
        result(i, i) = 1;
    return result;
}

template <typename T, typename E = expr_t<T>> ENOKI_INLINE Matrix<E, 1> invert(Matrix<T, 1> m) {
    using Vector = Array<E, 1>;
    return rcp<Vector::Approx>(m(0, 0));
}

template <typename T, typename E = expr_t<T>> ENOKI_INLINE Matrix<E, 2> invert(Matrix<T, 2> m) {
    using Vector = Array<E, 2>;
    E inv_det = rcp<Vector::Approx>(fmsub(m(0, 0), m(1, 1), m(0, 1) * m(1, 0)));
    return Matrix<E, 2>( m(1, 1) * inv_det, -m(0, 1) * inv_det,
                        -m(1, 0) * inv_det,  m(0, 0) * inv_det);
}

template <typename T, typename E = expr_t<T>> ENOKI_INLINE Matrix<E, 3> invert(Matrix<T, 3> m) {
    using Vector = Array<E, 3>;

    Vector col0 = m.coeff(0), col1 = m.coeff(1),
           col2 = m.coeff(2);

    Vector row0 = cross(col1, col2);
    Vector row1 = cross(col2, col0);
    Vector row2 = cross(col0, col1);

    Vector inv_det = Vector(rcp<Vector::Approx>(dot(col0, row0)));

    return transpose(Matrix<E, 3>(row0 * inv_det,
                                  row1 * inv_det,
                                  row2 * inv_det));
}

template <typename T, typename E = expr_t<T>> ENOKI_INLINE Matrix<E, 4> invert(Matrix<T, 4> m) {
    using Vector = Array<E, 4>;

    Vector col0 = m.coeff(0), col1 = m.coeff(1),
           col2 = m.coeff(2), col3 = m.coeff(3);

    col1 = shuffle<2, 3, 0, 1>(col1);
    col3 = shuffle<2, 3, 0, 1>(col3);

    Vector tmp, row0, row1, row2, row3;

    tmp = shuffle<1, 0, 3, 2>(col2 * col3);
    row0 = col1 * tmp;
    row1 = col0 * tmp;
    tmp = shuffle<2, 3, 0, 1>(tmp);
    row0 = fmsub(col1, tmp, row0);
    row1 = shuffle<2, 3, 0, 1>(fmsub(col0, tmp, row1));

    tmp = shuffle<1, 0, 3, 2>(col1 * col2);
    row0 = fmadd(col3, tmp, row0);
    row3 = col0 * tmp;
    tmp = shuffle<2, 3, 0, 1>(tmp);
    row0 = fnmadd(col3, tmp, row0);
    row3 = shuffle<2, 3, 0, 1>(fmsub(col0, tmp, row3));

    tmp = shuffle<1, 0, 3, 2>(shuffle<2, 3, 0, 1>(col1) * col3);
    col2 = shuffle<2, 3, 0, 1>(col2);
    row0 = fmadd(col2, tmp, row0);
    row2 = col0 * tmp;
    tmp = shuffle<2, 3, 0, 1>(tmp);
    row0 = fnmadd(col2, tmp, row0);
    row2 = shuffle<2, 3, 0, 1>(fmsub(col0, tmp, row2));

    tmp = shuffle<1, 0, 3, 2>(col0 * col1);
    row2 = fmadd(col3, tmp, row2);
    row3 = fmsub(col2, tmp, row3);
    tmp = shuffle<2, 3, 0, 1>(tmp);
    row2 = fmsub(col3, tmp, row2);
    row3 = fnmadd(col2, tmp, row3);

    tmp = shuffle<1, 0, 3, 2>(col0 * col3);
    row1 = fnmadd(col2, tmp, row1);
    row2 = fmadd(col1, tmp, row2);
    tmp = shuffle<2, 3, 0, 1>(tmp);
    row1 = fmadd(col2, tmp, row1);
    row2 = fnmadd(col1, tmp, row2);

    tmp = shuffle<1, 0, 3, 2>(col0 * col2);
    row1 = fmadd(col3, tmp, row1);
    row3 = fnmadd(col1, tmp, row3);
    tmp = shuffle<2, 3, 0, 1>(tmp);
    row1 = fnmadd(col3, tmp, row1);
    row3 = fmadd(col1, tmp, row3);

    Vector inv_det = Vector(rcp<Vector::Approx>(dot(col0, row0)));

    return transpose(Matrix<E, 4>(
        row0 * inv_det, row1 * inv_det,
        row2 * inv_det, row3 * inv_det
    ));
}

// =======================================================================
//! @{ \name Enoki accessors for static & dynamic vectorization
// =======================================================================

template <typename T, size_t Size> struct dynamic_support<Matrix<T, Size>, enable_if_static_array_t<Matrix<T, Size>>> {
    static constexpr bool is_dynamic_nested = enoki::is_dynamic_nested<T>::value;
    using dynamic_t = Matrix<enoki::make_dynamic_t<T>, Size>;
    using Value = Matrix<T, Size>;

    static ENOKI_INLINE size_t slices(const Value &value) {
        return enoki::slices(value.coeff(0, 0));
    }

    static ENOKI_INLINE size_t packets(const Value &value) {
        return enoki::packets(value.coeff(0, 0));
    }

    static ENOKI_INLINE void set_slices(Value &value, size_t size) {
        for (size_t i = 0; i < Size; ++i)
            enoki::set_slices(value.coeff(i), size);
    }

    template <typename T2>
    static ENOKI_INLINE auto packet(T2&& value, size_t i) {
        return packet(value, i, std::make_index_sequence<Size>());
    }

    template <typename T2>
    static ENOKI_INLINE auto slice(T2&& value, size_t i) {
        return slice(value, i, std::make_index_sequence<Size>());
    }

    template <typename T2>
    static ENOKI_INLINE auto slice_ptr(T2&& value, size_t i) {
        return slice_ptr(value, i, std::make_index_sequence<Size>());
    }

    template <typename T2>
    static ENOKI_INLINE auto ref_wrap(T2&& value) {
        return ref_wrap(value, std::make_index_sequence<Size>());
    }

private:
    template <typename T2, size_t... Index>
    static ENOKI_INLINE auto packet(T2&& value, size_t i, std::index_sequence<Index...>) {
        return Matrix<decltype(enoki::packet(value.coeff(0, 0), i)), Size>(
            enoki::packet(value.coeff(Index), i)...);
    }

    template <typename T2, size_t... Index>
    static ENOKI_INLINE auto slice(T2&& value, size_t i, std::index_sequence<Index...>) {
        return Matrix<decltype(enoki::slice(value.coeff(0, 0), i)), Size>(
            enoki::slice(value.coeff(Index), i)...);
    }

    template <typename T2, size_t... Index>
    static ENOKI_INLINE auto slice_ptr(T2&& value, size_t i, std::index_sequence<Index...>) {
        return Matrix<decltype(enoki::slice_ptr(value.coeff(0, 0), i)), Size>(
            enoki::slice_ptr(value.coeff(Index), i)...);
    }

    template <typename T2, size_t... Index>
    static ENOKI_INLINE auto ref_wrap(T2&& value, std::index_sequence<Index...>) {
        return Matrix<decltype(enoki::ref_wrap(value.coeff(0, 0))), Size>(
            enoki::ref_wrap(value.coeff(Index))...);
    }
};

//! @}
// =======================================================================

NAMESPACE_END(enoki)

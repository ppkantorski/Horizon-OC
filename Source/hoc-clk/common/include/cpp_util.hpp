/*
 * Copyright (c) meha3945 (hanai3bi)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#pragma once

#include <utility>

template<typename F>
class ScopeGuard {
public:
    ScopeGuard(F&& f)
     : f(f), engaged(true) {};

    ~ScopeGuard() {
        if (engaged)
            f();
    };

    ScopeGuard(ScopeGuard&& rhs)
     : f(std::move(rhs.f)) {};

    void dismiss() { engaged = false; }

private:
    F f;
    bool engaged;
};

struct MakeScopeExit {
    template<typename F>
    ScopeGuard<F> operator+=(F&& f) {
        return ScopeGuard<F>(std::move(f));
    };
};

#define STRING_CAT2(x, y) x##y
#define STRING_CAT(x, y) STRING_CAT2(x, y)
#define SCOPE_GUARD MakeScopeExit() += [&]() __attribute__((always_inline))
#define SCOPE_EXIT auto STRING_CAT(scope_exit_, __LINE__) = SCOPE_GUARD
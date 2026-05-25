// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

//
//  function_ref.hpp
//  lang
//
//  Non-owning view of a callable (cf. C++26 std::function_ref / llvm::function_ref).
//  Holds a pointer to the callable plus a thunk; never allocates and erases the
//  callable's type behind a single concrete instantiation, so a function taking
//  function_ref is compiled once out-of-line rather than monomorphized per call
//  site. The referenced callable must outlive the call -- only ever pass one that
//  is live for the duration of the invocation (e.g. a stack lambda at the call).
//

#ifndef function_ref_hpp
#define function_ref_hpp

#include <memory>
#include <type_traits>
#include <utility>

namespace ts {

template <typename Fn>
class function_ref;

template <typename R, typename... Args>
class function_ref<R(Args...)> {
public:
    function_ref() = delete;

    template <typename Callable,
              typename = std::enable_if_t<
                  !std::is_same_v<std::remove_cvref_t<Callable>, function_ref> &&
                  std::is_invocable_r_v<R, Callable&, Args...>>>
    function_ref(Callable&& callable) noexcept
        : obj_(const_cast<void*>(
              reinterpret_cast<void const*>(std::addressof(callable)))),
          thunk_(&thunkFor<std::remove_reference_t<Callable>>) {}

    R operator()(Args... args) const {
        return thunk_(obj_, std::forward<Args>(args)...);
    }

private:
    template <typename Callable>
    static R thunkFor(void* obj, Args... args) {
        return (*reinterpret_cast<Callable*>(obj))(std::forward<Args>(args)...);
    }

    void* obj_;
    R (*thunk_)(void*, Args...);
};

} // namespace ts

#endif // function_ref_hpp

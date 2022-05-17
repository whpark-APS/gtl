﻿//module;
//
//#include "gtl/gtl.h"
//#include "gtl.ui.h"
//
//export module gtl.ui:prop.value.etc;
//import :predefine;
//import :color;
//
//using namespace std::literals;
//using namespace gtl::literals;
//using namespace gtl::ui::unit::literals;


#pragma once

#include "ui_predefine.h"

/*export*/ namespace gtl::ui::prop::value {

	namespace text_align {
		static inline constexpr string_t const center		{ GText("center"s) };
		static inline constexpr string_t const left			{ GText("left"s) };
		static inline constexpr string_t const right		{ GText("right"s) };
		static inline constexpr string_t const justify		{ GText("justify"s) };
		static inline constexpr string_t const init			{ GText("init"s) };
		static inline constexpr string_t const inherit		{ GText("inherit"s) };
	}



}


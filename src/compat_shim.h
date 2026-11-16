// compat_shim.h - force-included before every source file (see .vcxproj ForcedIncludeFiles).
// Author: PZA. Licence: GPLv3 or later.
//
// Kenshi's Ogre headers (shipped inside KenshiLib\Include\ogre) were written for
// VS2010 and, when _MSC_VER >= 1600, refer to ::std::tr1::hash / unordered_map /
// unordered_set (see OgrePrerequisites.h lines ~98-102 and OgreString.h ~212).
// The modern MSVC STL (VS2019+/VS2022) no longer has the std::tr1 namespace, so
// we alias the C++11 names into it. Under the v100 toolset this block is skipped.
//
// WARNING (ABI): this only makes the headers *compile*. Any Ogre class that
// contains an OGRE_HashMap member (e.g. Ogre::OldNode::ChildOldNodeMap) has a
// different in-memory layout in Kenshi (VS2010 tr1) than what v143 thinks.
// Never read such members directly / via inline Ogre header code under v143.
#pragma once

#if defined(_MSC_VER) && (_MSC_VER >= 1900)
#include <functional>
#include <unordered_map>
#include <unordered_set>
namespace std {
namespace tr1 {
    using ::std::hash;
    using ::std::unordered_map;
    using ::std::unordered_multimap;
    using ::std::unordered_set;
    using ::std::unordered_multiset;
}
}
#endif

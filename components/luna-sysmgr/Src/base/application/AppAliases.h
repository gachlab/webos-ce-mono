// Who answers to an app id.
//
// OURS, not HP's (#63), and in a header of its own so the rule can be tested
// without standing up ApplicationManager, which is a singleton that scans
// directories. ApplicationManager::getAppById is a two-line call into this.
//
// In webOS an app id is an ADDRESS: the shell, notifications, activities and
// HP's own apps all open an app by id -- Contacts alone is named twelve times
// across the tree. So an app we write to replace one of HP's has to inherit its
// address, or everything that called the old one stops working. The app says so
// itself, in its appinfo.json:
//
//     { "id": "com.gachlab.app.contacts",
//       "aliases": ["com.palm.app.contacts"] }
//
// and this is the only place it is resolved. A table in the shell would have to
// grow with every rewrite; this does not.
//
// THE RULE, and the whole of it:
//
//   1. An app installed under that exact id wins. Registered apps before
//      system ones, which is the order HP looked in.
//   2. Only if nothing matches exactly does an alias count -- so an alias can
//      never take an address away from an app that is really installed. If
//      HP's own Contacts is on the device, it keeps com.palm.app.contacts and
//      ours is simply not reached by that name.
//   3. Nothing is translated on the way out. What an app is told it is, and
//      what it reports as PalmSystem.identifier, is still its own id.
//
// A template over anything whose elements have id() and aliases(), which is
// what lets the test hand it a two-field struct instead of the real thing.

#ifndef APPALIASES_H
#define APPALIASES_H

#include <list>
#include <string>

template <typename Apps>
typename Apps::value_type appAnsweringToIn(const Apps& apps, const std::string& appId)
{
	for (typename Apps::const_iterator it = apps.begin(); it != apps.end(); ++it) {
		if ((*it)->id() == appId)
			return *it;
	}
	return 0;
}

template <typename Apps>
typename Apps::value_type appAliasedToIn(const Apps& apps, const std::string& appId)
{
	for (typename Apps::const_iterator it = apps.begin(); it != apps.end(); ++it) {
		const std::list<std::string>& aliases = (*it)->aliases();
		for (std::list<std::string>::const_iterator alias = aliases.begin();
		     alias != aliases.end(); ++alias) {
			if (*alias == appId)
				return *it;
		}
	}
	return 0;
}

// Both lists, in HP's order, with the exact pass finished before the alias one
// begins. That ordering is rule 2, and writing it as four passes rather than
// two is the point: an alias in the first list must not beat an exact match in
// the second.
template <typename Apps>
typename Apps::value_type appAnsweringTo(const Apps& registered, const Apps& system,
                                         const std::string& appId)
{
	if (typename Apps::value_type app = appAnsweringToIn(registered, appId))
		return app;
	if (typename Apps::value_type app = appAnsweringToIn(system, appId))
		return app;
	if (typename Apps::value_type app = appAliasedToIn(registered, appId))
		return app;
	return appAliasedToIn(system, appId);
}

#endif // APPALIASES_H

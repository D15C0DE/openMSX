// $Id$

#ifndef STRINGSETTING_HH
#define STRINGSETTING_HH

#include "SettingPolicy.hh"
#include "SettingImpl.hh"

namespace openmsx {

class StringSettingPolicy : public SettingPolicy<std::string>
{
protected:
	explicit StringSettingPolicy();
	const std::string& toString(const std::string& value) const;
	const std::string& fromString(const std::string& str) const;
	std::string getTypeString() const;
};

class StringSetting : public SettingImpl<StringSettingPolicy>
{
public:
	StringSetting(CommandController& commandController,
	              const std::string& name, const std::string& description,
	              const std::string& initialValue, SaveSetting save = SAVE);
	StringSetting(CommandController& commandController,
	              const char* name, const char* description,
	              const char* initialValue, SaveSetting save = SAVE);
};

} // namespace openmsx

#endif

#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

class Argument
{
public:
	Argument()
	{
		used = false;
	}

	Argument(std::string shortName, std::string longName, std::string action, std::string help, std::string _default, bool required) : Argument()
	{
		this->shortName = shortName;
		this->longName = longName;

		if (shortName.size() == 0 && longName.size() == 0)
		{
			throw std::runtime_error("at least one name must be given");
		}

		this->action = action;
		this->help = help;
		this->_default = _default;
		this->required = required;

		if (this->action != "" && this->action != "store_true")
		{
			throw std::runtime_error("action must be empty or store_true");
		}

		if (this->action == "store_true" && this->required)
		{
			throw std::runtime_error("store_true arguments cannot be required");
		}

		if (this->action == "store_true")
		{
			this->_default = "false";
		}
	}

	int getNumArgsNeededAfter()
	{
		if (this->action == "store_true")
		{
			return 0;
		}

		return 1;
	}

	std::vector<std::string> getPossibleNames()
	{
		std::vector<std::string> retVec;
		if (this->shortName.size() > 0)
			retVec.push_back(this->shortName);
		if (this->longName.size() > 0)
			retVec.push_back(this->longName);

		return retVec;
	}

	std::string shortName;
	std::string longName;
	std::string action;
	std::string help;
	std::string _default;
	bool used;

	bool required;
};


class ArgumentParser
{
public:
	ArgumentParser()
	{

	}

	void parse_args(const char**argv, int argc)
	{
		int currentArg = 1;
		while (currentArg < argc)
		{
			std::string potentialArg = argv[currentArg];

			// remove --
			if (potentialArg[0] == '-')
			{
				potentialArg = potentialArg.substr(1);
			}
			if (potentialArg[0] == '-')
			{
				potentialArg = potentialArg.substr(1);
			}

			Argument* arg = NULL;
			for (auto &i : arguments)
			{
				if (i.longName == potentialArg || i.shortName == potentialArg)
				{
					arg = &i;
					if (arg->used)
					{
						throw std::runtime_error("arg with name " + potentialArg + " was already used");
					}
					arg->used = true;
					break;
				}
			}
			if (!arg)
			{
				throw std::runtime_error("no arg with name " + potentialArg + " found");
			}

			if (arg->action == "store_true")
			{
				parsedMap[potentialArg] = "true";
				parsedMap[getAltArgName(potentialArg)] = "true";

				currentArg++;
				continue;
			}

			// Grab 1 more arg.
			currentArg++;
			if (argc <= currentArg)
			{
				throw std::runtime_error("Seems like arg " + potentialArg + " is missing a param");
			}
			std::string value = argv[currentArg];
			parsedMap[potentialArg] = value;
			parsedMap[getAltArgName(potentialArg)] = value;

			currentArg++;
		}

		// See if all required ones were given
		for (auto &i : arguments)
		{
			if (i.required)
			{
				if (!(parsedMap.find(i.shortName) != parsedMap.end() || parsedMap.find(i.longName) != parsedMap.end()))
				{
					throw std::runtime_error("Missing required param: " + i.shortName + " / " + i.longName);
				}
			}
			else
			{
				if (parsedMap.find(i.shortName) == parsedMap.end() && parsedMap.find(i.longName) == parsedMap.end())
				{
					parsedMap[i.shortName] = i._default;
					parsedMap[i.longName] = i._default;
				}
			}
		}
	}

	void add_argument(Argument a)
	{
		arguments.push_back(a);
	}

	std::string getAltArgName(std::string argName)
	{
		for (auto &i : arguments)
		{
			if (i.shortName == argName)
			{
				return i.longName;
			}
			if (i.longName == argName)
			{
				return i.shortName;
			}
		}
		throw std::runtime_error("Invalid arg given to getAltArgName(): " + argName);
		return "";
	}

	unsigned long getNumericValue(std::string name)
	{
		return std::stoul(parsedMap[name]);
	}

	std::string getStringValue(std::string name)
	{
		return parsedMap[name];
	}

	bool getBooleanValue(std::string name)
	{
		if (parsedMap.find(name) != parsedMap.end())
		{
			return (parsedMap.find(name)->second == "true");
		}

		return false;
	}

private:
	std::vector<Argument> arguments;
	std::map<std::string, std::string> parsedMap;
};
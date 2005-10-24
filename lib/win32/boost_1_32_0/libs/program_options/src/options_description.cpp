// Copyright Vladimir Prus 2002-2004.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt
// or copy at http://www.boost.org/LICENSE_1_0.txt)


#define BOOST_PROGRAM_OPTIONS_SOURCE
#include <boost/program_options/config.hpp>
#include <boost/program_options/options_description.hpp>
// FIXME: this is only to get multiple_occureces class
// should move that to a separate headers.
#include <boost/program_options/parsers.hpp>

#include <boost/lexical_cast.hpp>

#include <boost/detail/workaround.hpp>

#include <cassert>
#include <climits>
#include <cstring>
#include <cstdarg>
#include <sstream>
using namespace std;

namespace boost { namespace program_options {

    option_description::option_description()
    {
    }
    
    option_description::
    option_description(const char* name,
                       const value_semantic* s)
    : m_value_semantic(s)
    {
        this->name(name);
    }
                                           

    option_description::
    option_description(const char* name,
                       const value_semantic* s,
                       const char* description)
    : m_description(description), m_value_semantic(s)
    {
        this->name(name);
    }

    option_description::~option_description()
    {
    }

    option_description&
    option_description::name(const char* _name)
    {
        std::string name(_name);
        string::size_type n = name.find(',');
        if (n != string::npos) {
            assert(n == name.size()-2);
            m_long_name = name.substr(0, n);
            m_short_name = name.substr(n+1,1);
        } else {
            m_long_name = name;
        }
        return *this;
    }

    const std::string&
    option_description::short_name() const
    {
        return m_short_name;
    }

    const std::string&
    option_description::long_name() const
    {
        return m_long_name;
    }

    const std::string&
    option_description::description() const
    {
        return m_description;
    }

    shared_ptr<const value_semantic>
    option_description::semantic() const
    {
        return m_value_semantic;
    }
    
    std::string 
    option_description::format_name() const
    {
        if (!short_name().empty())
            return string("-").append(short_name()).append(" [ --").
            append(long_name()).append(" ]");
        else
            return string("--").append(long_name());
    }

    std::string 
    option_description::format_parameter() const
    {
        if (!m_value_semantic->is_zero_tokens())
            return m_value_semantic->name();
        else
            return "";
    }

    options_description_easy_init::
    options_description_easy_init(options_description* owner)
    : owner(owner)
    {}

    options_description_easy_init&
    options_description_easy_init::
    operator()(const char* name,
               const char* description)
    {
        // Create untypes semantic which accepts zero tokens: i.e. 
        // no value can be specified on command line.
        // FIXME: does not look exception-safe
        shared_ptr<option_description> d(
            new option_description(name, new untyped_value(true), description));

        owner->add(d);
        return *this;
    }

    options_description_easy_init&
    options_description_easy_init::
    operator()(const char* name,
               const value_semantic* s)
    {
        shared_ptr<option_description> d(new option_description(name, s));
        owner->add(d);
        return *this;
    }

    options_description_easy_init&
    options_description_easy_init::
    operator()(const char* name,
               const value_semantic* s,
               const char* description)
    {
        shared_ptr<option_description> d(new option_description(name, s, description));

        owner->add(d);
        return *this;
    }

    options_description::options_description()
    {}

    options_description::options_description(const string& caption)
    : m_caption(caption)
    {}

    void
    options_description::add(shared_ptr<option_description> desc)
    {
        const string& s = desc->short_name();
        const string& l = desc->long_name();
        assert(!s.empty() || !l.empty());
        if (!s.empty())
            if (name2index.count("-" + s) != 0)
                throw duplicate_option_error("Short name '" + s + "' is already present");
            else
                name2index["-" + s] = options.size();
        if (!l.empty())
            if (name2index.count(s) != 0)
                throw duplicate_option_error("Long name '" + s + "' is already present");
            else
                name2index[l] = options.size();
        options.push_back(desc);
        belong_to_group.push_back(false);
    }

    options_description&
    options_description::add(const options_description& desc)
    {
        shared_ptr<options_description> d(new options_description(desc));
        groups.push_back(d);

        for (size_t i = 0; i < desc.options.size(); ++i) {
            add(desc.options[i]);
            belong_to_group.back() = true;
        }

        return *this;
    }

    options_description_easy_init
    options_description::add_options()
    {       
        return options_description_easy_init(this);
    }

    unsigned
    options_description::count(const std::string& name) const
    {
        return name2index.count(name);
    }

    unsigned
    options_description::count_approx(const std::string& prefix) const
    {
        approximation_range er = find_approximation(prefix);
        return distance(er.first, er.second);
    }

    const option_description&
    options_description::find(const std::string& name) const
    {
        assert(this->count(name) != 0);
        return *options[name2index.find(name)->second];
    }

    const option_description&
    options_description::find_approx(const std::string& prefix) const
    {
        approximation_range er = find_approximation(prefix);
        assert(distance(er.first, er.second) == 1);
        return *options[er.first->second];
    }

    std::set<std::string>
    options_description::keys() const
    {
        set<string> result;
        for (map<string, int>::const_iterator i = name2index.begin();
             i != name2index.end();
             ++i)
            result.insert(i->first);
        return result;
    }

    std::set<std::string>
    options_description::primary_keys() const
    {
        set<string> result;
        for (size_t i = 0; i < options.size(); ++i)
            if (options[i]->long_name().empty())
                result.insert("-" + options[i]->short_name());
            else
                result.insert(options[i]->long_name());
        return result;
    }

    std::set<std::string>
    options_description::approximations(const std::string& prefix) const
    {
        approximation_range er = find_approximation(prefix);
        set<string> result;
        for (name2index_iterator i = er.first; i != er.second; ++i)
            result.insert(i->first);
        return result;
    }

    options_description::approximation_range
    options_description::find_approximation(const std::string& prefix) const
    {
        name2index_iterator b = name2index.lower_bound(prefix);
        name2index_iterator e = name2index.upper_bound(prefix + char(CHAR_MAX));
        return make_pair(b, e);
    }


    BOOST_PROGRAM_OPTIONS_DECL
    std::ostream& operator<<(std::ostream& os, const options_description& desc)
    {
        desc.print(os);
        return os;
    }

    namespace {
        void format_one(std::ostream& os, const option_description& opt, 
                        unsigned first_column_width)
        {
            stringstream ss;
            ss << "  " << opt.format_name() << ' ' << opt.format_parameter();
            
            // Don't use ss.rdbuf() since g++ 2.96 is buggy on it.
            os << ss.str();
            
            if (!opt.description().empty()) {
                
                for(int pad = first_column_width - ss.str().size(); pad > 0; --pad) {
                    os.put(' ');
                }
                os << " : " << opt.description();
            }
        }
    }

    void 
    options_description::print(std::ostream& os) const
    {
        if (!m_caption.empty())
            os << m_caption << ":\n";

        /* Find the maximum width of the option column */
        unsigned width(24);
        unsigned i; // vc6 has broken for loop scoping
        for (i = 0; i < options.size(); ++i)
        {
            const option_description& opt = *options[i];
            stringstream ss;
            ss << "  " << opt.format_name() << ' ' << opt.format_parameter();
            width = max(width, static_cast<unsigned>(ss.str().size()));            
        }

        /* The options formatting style is stolen from Subversion. */
        for (i = 0; i < options.size(); ++i)
        {
            if (belong_to_group[i])
                continue;

            const option_description& opt = *options[i];

            format_one(os, opt, width);

            os << "\n";
        }

        for (unsigned j = 0; j < groups.size(); ++j) {            
            os << "\n" << *groups[j];
        }
    }

}}

// Copyright Vladimir Prus 2002-2004.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt
// or copy at http://www.boost.org/LICENSE_1_0.txt)


#include <boost/config.hpp>

#define BOOST_PROGRAM_OPTIONS_SOURCE
#include <boost/program_options/config.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/detail/cmdline.hpp>
#include <boost/program_options/detail/config_file.hpp>
#include <boost/program_options/environment_iterator.hpp>
#include <boost/program_options/detail/convert.hpp>

#include <boost/bind.hpp>


#if !defined(__GNUC__) || __GNUC__ < 3
#include <iostream>
#else
#include <istream>
#endif

#ifdef _WIN32
#include <stdlib.h>
#else
#include <unistd.h>
#endif

// The 'environ' should be declared in some cases. E.g. Linux man page says:
// (This variable must be declared in the user program, but is declared in 
// the header file unistd.h in case the header files came from libc4 or libc5, 
// and in case they came from glibc and _GNU_SOURCE was defined.) 
// To be safe, declare it here.

// It appears that on Mac OS X the 'environ' variable is not
// available to dynamically linked libraries.
// See: http://article.gmane.org/gmane.comp.lib.boost.devel/103843
// See: http://lists.gnu.org/archive/html/bug-guile/2004-01/msg00013.html
#if defined(__APPLE__) && defined(__DYNAMIC__)
#include <crt_externs.h>
#define environ (*_NSGetEnviron()) 
#else
#if defined(__MWERKS__)
#include <crtl.h>
#else
#if !defined(_WIN32) || defined(__COMO_VERSION__)
extern char** environ;
#endif
#endif
#endif

using namespace std;

namespace boost { namespace program_options {

#ifndef BOOST_NO_STD_WSTRING
    namespace {
        woption woption_from_option(const option& opt)
        {
            woption result;
            result.string_key = opt.string_key;
            result.position_key = opt.position_key;
            
            std::transform(opt.value.begin(), opt.value.end(),
                           back_inserter(result.value),
                           bind(from_utf8, _1));
            return result;
        }
    }

    basic_parsed_options<wchar_t>
    ::basic_parsed_options(const parsed_options& po)
    : description(po.description),
      utf8_encoded_options(po)
    {
        for (unsigned i = 0; i < po.options.size(); ++i)
            options.push_back(woption_from_option(po.options[i]));
    }
#endif

    namespace detail
    {
        void
        parse_command_line(cmdline& cmd, parsed_options& result);
    }

    common_command_line_parser::
    common_command_line_parser(const std::vector<std::string>& args)
    : m_style(0), m_desc(0), m_positional(0), m_args(args)
    {}

    parsed_options
    common_command_line_parser::run() const
    {
        parsed_options result(m_desc);
        detail::cmdline cmd(m_args, m_style);
        cmd.set_additional_parser(m_ext);

        if (m_desc) {
            set<string> keys = m_desc->primary_keys();
            for (set<string>::iterator i = keys.begin(); i != keys.end(); ++i) {
                const option_description& d = m_desc->find(*i);
                char s = d.short_name().empty() ? '\0' : d.short_name()[0];

                shared_ptr<const value_semantic> vs = d.semantic();
                char flags;
                if (vs->is_zero_tokens())
                    flags = '|';
                else
                    if (vs->is_implicit()) 
                        if (vs->is_multitoken())
                            flags = '*';
                        else
                            flags = '?';
                    else if (vs->is_multitoken())
                        flags = '+';
                    else flags = ':';

                cmd.add_option(d.long_name(), s, flags, 1);
            }
        }

        detail::parse_command_line(cmd, result);

        if (m_positional)
        {
            unsigned position = 0;
            for (unsigned i = 0; i < result.options.size(); ++i) {
                option& opt = result.options[i];
                if (opt.position_key != -1) {
                    if (position >= m_positional->max_total_count())
                    {
                        throw too_many_positional_options_error(
                            "too many positional options");
                    }
                    opt.string_key = m_positional->name_for_position(position);
                    ++position;
                }
            }
        }

        return result;        
    }


    namespace detail {
        void
        parse_command_line(cmdline& cmd, parsed_options& result)
        {
            int position(0);
            
            while(++cmd) {
                
                option n;
                
                if (cmd.at_option()) {
                    if (*cmd.option_name().rbegin() != '*') {
                        n.string_key = cmd.option_name();
                    }
                    else {
                        n.string_key = cmd.raw_option_name();
                    }
                    n.value = cmd.option_values();
                } else {
                    n.position_key = position++;
                    n.value.clear();
                    n.value.push_back(cmd.argument());
                }
                result.options.push_back(n);
            }
        }
    }

    template<class charT>
    basic_parsed_options<charT>
    parse_config_file(std::basic_istream<charT>& is, 
                      const options_description& desc)
    {    
        set<string> allowed_options;
        set<string> pm = desc.primary_keys();
        for (set<string>::iterator i = pm.begin(); i != pm.end(); ++i) {
            const option_description& d = desc.find(*i);

            if (d.long_name().empty())
                throw error("long name required for config file");

            allowed_options.insert(d.long_name());
        }

        // Parser return char strings
        parsed_options result(&desc);        
        copy(detail::basic_config_file_iterator<charT>(is, allowed_options), 
             detail::basic_config_file_iterator<charT>(), 
             back_inserter(result.options));
        // Convert char strings into desired type.
        return basic_parsed_options<charT>(result);
    }

    template
    BOOST_PROGRAM_OPTIONS_DECL basic_parsed_options<char>
    parse_config_file(std::basic_istream<char>& is, 
                      const options_description& desc);

#ifndef BOOST_NO_STD_WSTRING
    template
    BOOST_PROGRAM_OPTIONS_DECL basic_parsed_options<wchar_t>
    parse_config_file(std::basic_istream<wchar_t>& is, 
                      const options_description& desc);
#endif
    
// This versio, which accepts any options without validation, is disabled,
// in the hope that nobody will need it and we cant drop it altogether.
// Besides, probably the right way to handle all options is the '*' name.
#if 0
    BOOST_PROGRAM_OPTIONS_DECL parsed_options
    parse_config_file(std::istream& is)
    {
        detail::config_file_iterator cf(is, false);
        parsed_options result(0);
        copy(cf, detail::config_file_iterator(), 
             back_inserter(result.options));
        return result;
    }
#endif

    BOOST_PROGRAM_OPTIONS_DECL parsed_options
    parse_environment(const options_description& desc, 
                      const function1<std::string, std::string>& name_mapper)
    {
        parsed_options result(&desc);
        
        for(environment_iterator i(environ), e; i != e; ++i) {
            string option_name = name_mapper(i->first);

            if (!option_name.empty()) {
                option n;
                n.string_key = option_name;
                n.value.push_back(i->second);
                result.options.push_back(n);
            }                
        }

        return result;
    }

    namespace {
        class prefix_name_mapper {
        public:
            prefix_name_mapper(const std::string& prefix)
            : prefix(prefix)
            {}

            std::string operator()(const std::string& s)
            {
                string result;
                if (s.find(prefix) == 0) {
                    for(string::size_type n = prefix.size(); n < s.size(); ++n) 
                    {   
                        // Intel-Win-7.1 does not understand
            // push_back on string.         
                        result += tolower(s[n]);
                    }
                }
                return result;
            }
        private:
            std::string prefix;
        };
    }

    BOOST_PROGRAM_OPTIONS_DECL parsed_options
    parse_environment(const options_description& desc, 
                      const std::string& prefix)
    {
        return parse_environment(desc, prefix_name_mapper(prefix));
    }

    BOOST_PROGRAM_OPTIONS_DECL parsed_options
    parse_environment(const options_description& desc, const char* prefix)
    {
        return parse_environment(desc, string(prefix));
    }




}}

// Copyright Vladimir Prus 2002-2004.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt
// or copy at http://www.boost.org/LICENSE_1_0.txt)

#define BOOST_PROGRAM_OPTIONS_SOURCE
#include <boost/program_options/config.hpp>

#include <boost/config.hpp>

#include <boost/program_options/detail/cmdline.hpp>
#include <boost/program_options/errors.hpp>

#include <string>
#include <utility>
#include <vector>
#include <cassert>
#include <cstring>
#include <cctype>

#include <cstdio>

namespace boost { namespace program_options {

    using namespace std;
    using namespace boost::program_options::command_line_style;
    
    invalid_command_line_syntax::
    invalid_command_line_syntax(const std::string& tokens, kind_t kind)
    : invalid_syntax(tokens, error_message(kind)), m_kind(kind)
    {}

    std::string 
    invalid_command_line_syntax::error_message(kind_t kind)
    {
        // Initially, store the message in 'const char*' variable,
        // to avoid conversion to std::string in all cases.
        const char* msg;
        switch(kind)
        {
        case long_not_allowed:
            msg = "long options are not allowed";
            break;
        case long_adjacent_not_allowed:
            msg = "parameters adjacent to long options not allowed";
            break;
        case short_adjacent_not_allowed:
            msg = "parameters adjust to short options are not allowed";
            break;
        case empty_adjacent_parameter:
            msg = "adjacent parameter is empty";
            break;
        case missing_parameter:
            msg = "required parameter is missing";
            break;
        case extra_parameter:
            msg = "extra parameter";
            break;
        default:
            msg = "unknown error";
        }
        return msg;
    }

    invalid_command_line_syntax::kind_t 
    invalid_command_line_syntax::kind() const
    {
        return m_kind;
    }


}}


namespace boost { namespace program_options { namespace detail {

    // vc6 needs this, but borland chokes when this is added.
#if BOOST_WORKAROUND(_MSC_VER, <= 1200)
    using namespace std;
    using namespace program_options;
#endif

    cmdline::cmdline(const std::vector<std::string>& args, int style,
                     bool allow_unregistered)
    {
        init(args, style, allow_unregistered);
    }

    cmdline::cmdline(int argc, const char*const * argv, int style,
                     bool allow_unregistered)
    {
#if defined(BOOST_NO_TEMPLATED_ITERATOR_CONSTRUCTORS)
        vector<string> args;
        copy(argv+1, argv+argc, inserter(args, args.end()));
        init(args, style, allow_unregistered);
#else
        init(vector<string>(argv+1, argv+argc), style, allow_unregistered);
#endif
    }

    void
    cmdline::init(const std::vector<std::string>& args, int style,
                  bool allow_unregistered)
    {
        if (style == 0) 
            style = default_style;        

        check_style(style);

        this->args = args;        
        this->style = style_t(style);
        this->allow_unregistered = allow_unregistered,

        index = 0;
        m_current = 0;
        m_next = 0;
        pending_short_option = 0;
        m_no_more_options = false;
        m_error_description = ed_success;
        m_num_tokens = 0;
    }

    void 
    cmdline::check_style(int style) const
    {
        bool allow_some_long = 
            (style & allow_long) || (style & allow_long_disguise);

        const char* error = 0;
        if (allow_some_long && 
            !(style & long_allow_adjacent) && !(style & long_allow_next))
            error = "style disallows parameters for long options";

        if (!error && (style & allow_short) &&
            !(style & short_allow_adjacent) && !(style & short_allow_next))
            error = "style disallows parameters for short options";

        if (!error && (style & allow_short) &&
            !(style & allow_dash_for_short) && !(style & allow_slash_for_short))
            error = "style disallows all characters for short options";

        if (error)
            throw invalid_command_line_style(error);

        // Need to check that if guessing and long disguise are enabled
        // -f will mean the same as -foo
    }

    void 
    cmdline::set_additional_parser(additional_parser p)
    {
        m_additional_parser = p;
    }

    void
    cmdline::add_option(const std::string& long_name, char short_name,
                        char properties, int index)
    {
        options.push_back(option(long_name, short_name, 
                                 translate_property(properties), index));
    }

    void 
    cmdline::add_option(const char* long_name, char short_name,
                        char properties, int index)
    {
        add_option(string(long_name), short_name, properties, index);
    }

    cmdline& 
    cmdline::operator++()
    {
        next();
        clear_error();
        return *this;
    }

    bool
    cmdline::at_option() const
    {
        return m_element_kind == ek_option;
    }

    bool
    cmdline::at_argument() const
    {
        return m_element_kind == ek_argument;
    }

    void
    cmdline::next()
    {
        if (!*this)
            return;

        // Skip over current element
        advance(m_num_tokens);

        // We might have consumed all tokens by now.
        if (!*this)
            return;

        m_last = m_current;

        m_opt = 0;
        m_num_tokens = 0;
        m_disguised_long = false;

        m_option_name = std::string();
        m_option_values.clear();
        m_argument = std::string();

        m_element_kind = ek_option;
        if (pending_short_option) {
            if (handle_short_option(pending_short_option))
                m_option_index = m_opt->index;
            // TODO: should decide what to do in this case
            assert(!m_disguised_long);
        } else {

            if (m_additional_parser) {
                pair<string, string> p = m_additional_parser(m_current);
                if (!p.first.empty())
                    if (handle_additional_parser(p)) {                        
                        m_option_index = m_opt ? m_opt->index : 1;
                        return;
                    } else {
                        // handle_additional_parser should have set
                        // error code accordingly.
                        return;
                    }
            }

            switch(is_option(m_current)) {
            case error_option:
                break;
            case no_option:
                if (strcmp(m_current, "--") == 0) {
                    m_no_more_options = true;
                    advance(1);                 
                    next();
                    return;
                } else {
                    m_element_kind = ek_argument;
                    m_argument = m_current;
                    m_arguments.push_back(m_argument);
                    m_num_tokens = 1;
                }
                break;
            case long_option:
                if (handle_long_option(m_current + 2))
                    m_option_index = m_opt ? m_opt->index : 1;
                break;
            case short_option:
                if (handle_short_option(m_current + 1))
                    m_option_index = m_opt ? m_opt->index : 1;
                break;
            case dos_option:
                if (handle_dos_option(m_current + 1))
                    m_option_index = m_opt ? m_opt->index : 1;
                break;
            }
        }
    }

    const string&
    cmdline::argument() const
    {
        return m_argument;
    }

    const string&
    cmdline::option_name() const
    {
        return m_option_name;
    }

    int 
    cmdline::option_index() const
    {
        return m_option_index;
    }

    const string&
    cmdline::raw_option_name() const
    {
        return m_raw_option_name;
    }

    const string&
    cmdline::option_value() const
    {
        static string empty;
        if (m_option_values.size() > 1)
            throw multiple_values("multiple values");
        return m_option_values.empty() ? empty : m_option_values.front();
    }

    const std::vector<std::string>& 
    cmdline::option_values() const
    {
        return m_option_values;
    }

    const vector<string>&
    cmdline::arguments() const
    {
        return m_arguments;
    }

    const string&
    cmdline::last() const
    {
        return m_last;
    }

    namespace detail {
        int strncmp_nocase(const char* s1, const char* s2, size_t n)
        {
            size_t i(0);
            for(;*s1 && *s2 && i < n; ++s1, ++s2, ++i) {                
                char c1 = *s1; 
                char c2 = *s2; 
                if (c1 == c2)
                    continue;
                c1 = tolower(*s1);
                c2 = tolower(*s2);
                if (c1 < c2)
                    return -1;
                else if (c1 > c2)
                    return 1;
            }
            if (i == n)
                return 0;
            else
                if (!*s1 && *s2)
                    return -1;
                else if (*s1 && !*s2)
                    return 1;
                else
                    return 0;
        }

        // Standard strncmp has "C" linkage and Comeau compiler
        // issues error when we select between strncmp_nocase
        // and strncmp using ?:, below
        int strncmp_case(const char* s1, const char* s2, size_t n)        
        {
            // At least intel-win32-7.1-vc6 does not like "std::" prefix below,
            // so add using directive make everyone happy
            using namespace std;

            // But some msvc version don't like using directive :-(
#if BOOST_WORKAROUND(_MSC_FULL_VER, >= 13102292) &&\
    BOOST_WORKAROUND(_MSC_FULL_VER, BOOST_TESTED_AT(13103077))
            return std::strncmp(s1, s2, n);
#else
            return strncmp(s1, s2, n);
#endif            
        }
    }

    
    void test_cmdline_detail()
    {
        using detail::strncmp_nocase;
        assert(strncmp_nocase("a", "a", 5) == 0);
        assert(strncmp_nocase("a", "d", 5) < 0);
        assert(strncmp_nocase("d", "a", 5) > 0);
        assert(strncmp_nocase("abc", "abcd", 4) < 0);
        assert(strncmp_nocase("abcd", "abc", 4) > 0);
        assert(strncmp_nocase("abcd", "abc", 3) == 0);
    }

    const cmdline::option*
    cmdline::find_long_option(const char* name)
    {
        // some systems does not have strchr et.al. in namespace std
        using namespace std;
       
        // Handle the case of '=' in name, which is not part of option name
        const char* eq = strchr(name, '=');
        // Comeau reports ambiguity between C (global) and C++ (std::) versions.
#if BOOST_WORKAROUND(__COMO__, BOOST_TESTED_AT(4303))
        std::size_t n = eq ? eq - name : std::strlen(name);
#else
        std::size_t n = eq ? eq - name : strlen(name);
#endif

        int (*cmp)(const char*, const char*, size_t);
        cmp = (style & case_insensitive) 
            ? detail::strncmp_nocase : detail::strncmp_case;
        const option* result = 0;
        for (size_t i = 0; i < options.size(); ++i) {
            const char* known_name = options[i].long_name.c_str();
            bool prefix = (*options[i].long_name.rbegin() == '*');
            std::size_t n2 = n;
            if (prefix)
                n2 = options[i].long_name.size()-1;
            if (cmp(name, known_name, n2) == 0) {
                // Is there match without guessing?
                if (options[i].long_name.size() == n2
                    || (prefix && options[i].long_name.size() > n2)) {
                    result = &options[i];
                    break;
                } else if (style & allow_guessing) {
                    if (result) {
                        result = 0;
                        m_error_description = ed_ambiguous_option;
                        break;
                    } else {
                        result = &options[i];
                    }
                }
            }
        }
        if (!result && m_error_description == ed_success)
            m_error_description = ed_unknown_option;
        return result;
    }

    const cmdline::option*
    cmdline::find_short_option(char name)
    {
        for (size_t i = 0; i < options.size(); ++i) {
            if (name == options[i].short_name)
                return &options[i];
        }
        m_error_description = ed_unknown_option;
        return 0;
    }


    bool
    cmdline::handle_long_option(const char* s)
    {
        // some systems does not have strchr et.al. in namespace std
        using namespace std;

        const option* opt = find_long_option(s);
        m_opt = opt;

        if (opt || allow_unregistered) {
            // We always use the long name as specified by the
            // user, not abbreviation or otherwise-cased one we
            // get on the command line.
            if (opt)
                m_option_name = opt->long_name;

            bool adjacent_parameter(false), next_parameter(false);
            const char* eq = strchr(s, '=');
            if (eq) {
                // But store option spelling from command line as well.
                m_raw_option_name = string(s, eq);
                if (eq[1]) {
                    if (!(style & long_allow_adjacent)) {
                        m_error_description = ed_long_adjacent_not_allowed;
                        return false;
                    } else {
                        adjacent_parameter = true;
                        m_option_values.push_back(string(eq+1));
                    }
                } else {
                    m_error_description = ed_empty_adjacent_parameter;
                    return false;
                }
            } else {
                m_raw_option_name = s;
                if (m_next && is_option(m_next) == no_option
                    && (style & long_allow_next)) {
                    next_parameter = true;
                }
                m_error_description = ed_success;
            }

            if (!opt)
                m_option_name = m_raw_option_name;

            return process_parameter(opt, adjacent_parameter, next_parameter);

        } else {
            // Option not found, 'find_long_option' has set error code already.
            return false;
        }
    }

    bool
    cmdline::handle_short_option(const char* s, bool ignore_sticky)
    {
        pending_short_option = 0;

        if (style & allow_long_disguise) {
            const option* opt = find_long_option(s);
            m_opt = opt;
            if (opt) {
                m_disguised_long = true;
                return handle_long_option(s);
            }
            else
                m_error_description = ed_success;
        }

        m_disguised_long = false;
        const option* opt = find_short_option(*s);
        m_opt = opt;

        if (opt || allow_unregistered) {
            
            if (opt && !opt->long_name.empty()) 
                m_option_name = opt->long_name;
            else
                m_option_name = '-' + string(s, s+1);

            m_raw_option_name = string(s, 1);

            bool adjacent_parameter(false), next_parameter(false);
            if (s[1] != '\0') {
                if (!(style & short_allow_adjacent)) {
                    m_error_description = ed_short_adjacent_not_allowed;
                    return false;
                } else {
                    adjacent_parameter = true;
                    m_option_values.push_back(string(s+1));
                }
            } else {

                if ((style & short_allow_next) && m_next) {
                    option_kind kind = is_option(m_next);

                    if (kind == no_option) {
                        next_parameter = true;                        
                    } else if (kind == short_option && opt 
                               && opt->properties == require_parameter)                               
                    {
                        // This handles a special case:
                        // -a -1
                        // where "-1" is a parameter to "-a". It's pretty
                        // hard to quote "-1" in any way on the comment line, so
                        // we decide that if "-a" has required parameter then -1 
                        // is the parameter.
                        // We do so even if there's registered "-1" option, 
                        // since:
                        //   - that how getopt works
                        //   - it allows command line to be parsed, we'll
                        //     get error otherwise.
                        next_parameter = true;
                    }                    
                }
                // Reset error state that 'is_option' might have changed
                m_error_description = ed_success;
            }

            bool ok = process_parameter(opt, adjacent_parameter, next_parameter);
            if (!ok && m_error_description == ed_extra_parameter
                && (style & allow_sticky) && !ignore_sticky)
                if (find_short_option(s[1]) != 0) {
                    m_error_description = ed_success;
                    m_option_values.clear();
                    pending_short_option = s+1;
                    m_num_tokens = 0;
                    ok = true;
                } else {
                    m_error_description = ed_extra_parameter;
                }
            return ok;
        } else {
            return false;
        }
    }

    bool
    cmdline::handle_dos_option(const char* s)
    {
        return handle_short_option(s, true);
    }

    bool
    cmdline::handle_additional_parser(const std::pair<string, string>& p)
    {
        m_option_name = p.first;
        m_raw_option_name = p.first;
        m_option_values.push_back(p.second);

        if (p.first[0] == '-')
            m_opt = find_short_option(p.first[1]);
        else 
            m_opt = find_long_option(p.first.c_str());
        
        if (m_opt && !m_opt->long_name.empty()) 
            m_option_name = m_opt->long_name;
        else
            m_option_name = "-" + p.first.substr(1,1);
        
        return process_parameter(m_opt, !p.second.empty(), false);
    }

    /* Handles parameter assignments, setting m_option_value and 
       m_num_tokens.

       'opt' describes the detected option. If it's 0, it means the option
       is not registered, but the parser allows unregistered options. Assumes
       that this option allows but not requires a parameter.

       'adjacent_parameter' says if there's a parameter in the same token as
       the option. In which case it must be already assigned to m_option_value.

       'next_parameter' says if there's next token, which can be interpreted as
       argument.
    */
    bool
    cmdline::process_parameter(const option* opt, bool adjacent_parameter,
                               bool next_parameter)
    {
        properties_t properties;        
        if (opt)
            properties = opt->properties;
        else
            properties = allow_parameter;
        bool accept_parameter((properties == allow_parameter)
                              || (properties == require_parameter)
                              || (properties == allow_parameters)
                              || (properties == require_parameters));

        bool ok(true);
        if (accept_parameter) {
            if (adjacent_parameter) {
                // Everything assigned already
                m_num_tokens = 1;
            } else {
                if (next_parameter) {
                    m_option_values.push_back(m_next);
                    m_num_tokens = 2;
                } else {
                    // No, there's no parameter at all!
                    if (properties == require_parameter) {
                        m_error_description = ed_missing_parameter;
                        ok = false;
                    } else {
                        m_num_tokens = 1;
                    }
                }
            }
        } else {
            if (adjacent_parameter) {
                m_error_description = ed_extra_parameter;
                ok = false;
            } else {
                m_num_tokens = 1;
            }
        }
        // If multiple parameters are allowed, consume every non-option
        // token
        if (properties == allow_parameters || properties == require_parameters) 
        {
            // Don't use m_current and m_next, but directly iterate over
            // input.
            for(size_t i = index + 2; 
                i < args.size() && is_option(args[i].c_str()) == no_option 
                && args[i] != "--"; 
                ++i, ++m_num_tokens) {
                m_option_values.push_back(args[i]);
                }
            m_error_description = ed_success;
        }
        return ok;
    }

    cmdline::operator bool() const
    {
        return index < args.size() && m_error_description == ed_success;
    }

    cmdline::option_kind
    cmdline::is_option(const char* s)
    {
        if (m_no_more_options)
            return no_option;

        if (*s == '-' && *(s+1) == '-' && *(s+2) != '\0')
            if (style & allow_long)
                return long_option;
            else {
                m_error_description = ed_long_not_allowed;
                return error_option;
            }

        if (style & allow_short)
        {
            if ((style & allow_dash_for_short) && *s == '-' && *(s+1) != '-' 
                && *(s+1) != '\0')
                return short_option;
            if ((style & allow_slash_for_short) && *s == '/')
                return dos_option;
        }

        return no_option;
    }

    void
    cmdline::advance(int count)
    {
        index += count;
        // Note that the 'args' array is not modified at all,
        // therefore storing results of c_str() is OK.
        m_current = index < args.size()? args[index].c_str() : 0;
        m_next = index+1 < args.size() ? args[index+1].c_str() : 0;
    }

    cmdline::properties_t 
    cmdline::translate_property(char p)
    {
        if (p == '|')
            return no_parameter;
        else if (p == '?')
            return allow_parameter;
        else if (p == ':')
            return require_parameter;
        else if (p == '*')
            return allow_parameters;
        else if (p == '+')
            return require_parameters;
        else
            throw logic_error("Invalid property character");
    }

    void 
    cmdline::clear_error()
    {
        error_description_t e = m_error_description;
        m_error_description = ed_success;

        invalid_command_line_syntax::kind_t re;

        // FIXME: have no idea why g++ 3.2 wants it.
        typedef boost::program_options::unknown_option unknown_option;
        typedef boost::program_options::ambiguous_option ambiguous_option;

        if (e) {
            if (e == ed_unknown_option)
                throw unknown_option(m_current);
            if (e == ed_ambiguous_option)
                throw ambiguous_option(m_current, vector<string>());

            switch(e) {
            case ed_long_not_allowed:
                re = invalid_command_line_syntax::long_not_allowed;
                break;
            case ed_long_adjacent_not_allowed:
                re = invalid_command_line_syntax::long_adjacent_not_allowed;
                break;
            case ed_short_adjacent_not_allowed:
                re = invalid_command_line_syntax::short_adjacent_not_allowed;
                break;
            case ed_empty_adjacent_parameter:
                re = invalid_command_line_syntax::empty_adjacent_parameter;
                break;
            case ed_missing_parameter:
                re = invalid_command_line_syntax::missing_parameter;
                break;
            case ed_extra_parameter:
                re = invalid_command_line_syntax::extra_parameter;
                break;
            default:
                ; // do nothing
            }
            throw invalid_command_line_syntax(m_current, re);
        }        
    }


}}}

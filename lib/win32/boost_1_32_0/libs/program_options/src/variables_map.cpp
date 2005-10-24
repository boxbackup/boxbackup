// Copyright Vladimir Prus 2002-2004.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt
// or copy at http://www.boost.org/LICENSE_1_0.txt)


#define BOOST_PROGRAM_OPTIONS_SOURCE
#include <boost/program_options/config.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/variables_map.hpp>

#include <cassert>

namespace boost { namespace program_options {

    using namespace std;

    // First, performs semantic actions for 'oa'. 
    // Then, stores in 'm' all options that are defined in 'desc'. 
    BOOST_PROGRAM_OPTIONS_DECL 
    void store(const parsed_options& options, variables_map& xm,
               bool utf8)
    {       
        assert(options.description);
        const options_description& desc = *options.description;

        // We need to access map's operator[], not the overriden version
        // variables_map. Ehmm.. messy.
        std::map<std::string, variable_value>& m = xm;

        // The set of existing values that should not be changed.
        std::set<std::string> final;
        for (map<string, variable_value>::iterator k = m.begin(); 
             k != m.end(); 
             ++k) 
        {
            if (!k->second.defaulted()) {
                // TODO: what if we have different definition
                // for the same option name during different calls
                // 'store'.
                bool composing = desc.count(k->first)
                    && desc.find(k->first).semantic()->is_composing();

                if (!composing)
                    final.insert(k->first);
            }
        }

        // First, convert/store all given options
        for (size_t i = 0; i < options.options.size(); ++i) {

            const string& name = options.options[i].string_key;
            // Skip positional options without name
            if (name.empty())
                continue;

            // If option has final value, skip this assignment
            if (final.count(name))
                continue;

            // Ignore options which are not described
            if (desc.count(name) == 0)
                continue;

            const option_description& d = desc.find(name);

            variable_value& v = m[name];            
            if (v.defaulted()) {
                // Explicit assignment here erases defaulted value
                v = variable_value();
            }
            try {
                d.semantic()->parse(v.value(), options.options[i].value, utf8);
            }
            catch(validation_error& e)
            {
                e.set_option_name(name);
                throw;
            }
            v.m_value_semantic = d.semantic();
        }
        
        // Second, apply default values.
        set<string> keys = desc.primary_keys();
        for (set<string>::const_iterator j = keys.begin(); j != keys.end(); ++j)
            if (m.count(*j) == 0) {
                const option_description& d = desc.find(*j);

                boost::any def;
                if (d.semantic()->apply_default(def)) {
                    m[*j] = variable_value(def, true);
                    m[*j].m_value_semantic = d.semantic();
                }
            }        
    }

    BOOST_PROGRAM_OPTIONS_DECL 
    void store(const wparsed_options& options, variables_map& m)
    {
        store(options.utf8_encoded_options, m, true);
    }

    BOOST_PROGRAM_OPTIONS_DECL 
    void notify(variables_map& vm)
    {        
        // Lastly, run notify actions.
        for (map<string, variable_value>::iterator k = vm.begin(); 
             k != vm.end(); 
             ++k) 
        {
            k->second.m_value_semantic->notify(k->second.value());
        }               
    }

    abstract_variables_map::abstract_variables_map()
    : m_next(0)
    {}

    abstract_variables_map::
    abstract_variables_map(const abstract_variables_map* next)
    : m_next(next)
    {}

    const variable_value& 
    abstract_variables_map::operator[](const std::string& name) const
    {
        const variable_value& v = get(name);
        if (v.empty() && m_next)
            return (*m_next)[name];
        else if (v.defaulted() && m_next) {
            const variable_value& v2 = (*m_next)[name];
            if (!v2.empty() && !v2.defaulted())
                return v2;
            else return v;
        } else {
            return v;
        }
    }

    void 
    abstract_variables_map::next(abstract_variables_map* next)
    {
        m_next = next;
    }

    variables_map::variables_map()
    {}

    variables_map::variables_map(const abstract_variables_map* next)
    : abstract_variables_map(next)
    {}

    const variable_value&
    variables_map::get(const std::string& name) const
    {
        static variable_value empty;
        const_iterator i = this->find(name);
        if (i == this->end())
            return empty;
        else
            return i->second;
    }
}}

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

// This is the first gcc header to be included
#include "gcc-plugin.h"
#include "plugin-version.h"

#include "tree-pass.h"
#include "context.h"
#include "function.h"
#include "tree.h"
#include "tree-ssa-alias.h"
#include "internal-fn.h"
#include "is-a.h"
#include "predict.h"
#include "basic-block.h"
#include "gimple-expr.h"
#include "gimple.h"
#include "gimple-pretty-print.h"
#include "gimple-iterator.h"
#include "gimple-walk.h"


// We must assert that this plugin is GPL compatible
int plugin_is_GPL_compatible;

static struct plugin_info my_gcc_plugin_info = {
    "1.0", "Plugin for creating dump of basic blocks and GIMPLE statements into DOT language."
};


namespace
{
std::string
escapeDoubleQuotes( const std::string& toEscape )
{
    std::string result;
    for ( const auto c : toEscape ) {
        if ( c == '"' ) {
            result += '\\';
        }
        result += c;
    }
    return result;
}


const pass_data my_first_pass_data =
{
    GIMPLE_PASS,
    "my_first_pass",        /* name */
    OPTGROUP_NONE,          /* optinfo_flags */
    TV_NONE,                /* tv_id */
    PROP_gimple_any,        /* properties_required */
    0,                      /* properties_provided */
    0,                      /* properties_destroyed */
    0,                      /* todo_flags_start */
    0                       /* todo_flags_finish */
};

struct my_first_pass :
    public gimple_opt_pass
{
public:
    my_first_pass(gcc::context *ctx) :
        gimple_opt_pass(my_first_pass_data, ctx),
        m_tmpFile( fopen( "temporary-file-for-dumpying-gimple-sequence", "w+b" ) )
    {
        std::cerr << "digraph cfg {\n";
    }

    virtual unsigned int
    execute(function *fun) override
    {
        basic_block bb;

        std::cerr << "\nsubgraph fun_" << fun << " {\n";

        FOR_ALL_BB_FN(bb, fun)
        {
            gimple_bb_info *bb_info = &bb->il.gimple;

            std::cerr << "    bb_" << fun << "_" << bb->index << "[label=\"";

            std::stringstream label;
            if (bb->index == 0) {
                label
                    << "ENTRY: "
                    << function_name(fun) << "\n"
                    << (LOCATION_FILE(fun->function_start_locus) ? : "<unknown>") << ":"
                    << LOCATION_LINE(fun->function_start_locus);
            } else if (bb->index == 1) {
                label
                    << "EXIT: "
                    << function_name(fun) << "\n"
                    << (LOCATION_FILE(fun->function_end_locus) ? : "<unknown>") << ":"
                    << LOCATION_LINE(fun->function_end_locus);
            } else {
                fseek( m_tmpFile, 0, SEEK_SET );
                print_gimple_seq(m_tmpFile, bb_info->seq , 0, static_cast<dump_flags_t>(0));
                const auto writtenCount = ftell( m_tmpFile );
                fseek( m_tmpFile, 0, SEEK_SET );
                std::vector<char> buffer( writtenCount );
                const auto readBytes = fread( buffer.data(), 1, writtenCount, m_tmpFile );
                label << std::string( buffer.data(), writtenCount );
            }
            std::cerr << escapeDoubleQuotes( std::move( label ).str() ) << "\"];\n\n";

            edge e;
            edge_iterator ei;

            FOR_EACH_EDGE (e, ei, bb->succs)
            {
                basic_block dest = e->dest;
                std::cerr << "    bb_" << fun << "_" << bb->index << " -> bb_" << fun << "_" << dest->index << ";\n";
            }

            std::cerr << "\n";
        }

        std::cerr << "}\n";

        return 0;
    }

    virtual my_first_pass* clone() override
    {
        // We do not clone ourselves
        return this;
    }

private:
    /* Worst hack I've done in a while but I don't see an alternative to print_gimple_seq that prints to a buffer. */
    FILE* m_tmpFile;
};


void finish_gcc(void *gcc_data,
                void *user_data)
{
    std::cerr << "}\n";
}
}

int plugin_init(struct plugin_name_args   *plugin_info,
                struct plugin_gcc_version *version)
{
	// We check the current gcc loading this plugin against the gcc we used to
	// created this plugin
	if (!plugin_default_version_check (version, &gcc_version))
    {
        std::cerr << "This GCC plugin is for version " << GCCPLUGIN_VERSION_MAJOR << "." << GCCPLUGIN_VERSION_MINOR << "\n";
		return 1;
    }

    register_callback(plugin_info->base_name, /* event */ PLUGIN_INFO,
                      /* callback */ NULL, /* user_data */ &my_gcc_plugin_info);

    // Register the phase right after omplower
    struct register_pass_info pass_info;

    pass_info.pass = new my_first_pass(g);
    pass_info.reference_pass_name = "ssa";
    pass_info.ref_pass_instance_number = 1;
    pass_info.pos_op = PASS_POS_INSERT_AFTER;

    register_callback (plugin_info->base_name, PLUGIN_PASS_MANAGER_SETUP, NULL, &pass_info);
    register_callback (plugin_info->base_name, PLUGIN_FINISH, finish_gcc, NULL);

    return 0;
}

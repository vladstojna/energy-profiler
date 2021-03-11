// config.cpp

#include "config.hpp"

#include <iostream>
#include <cstring>

#include <pugixml.hpp>

using namespace tep;

static const char* error_messages[] =
{
    "No error",
    "I/O error when loading config file",
    "Config file not found",
    "Out of memory when loading config file",
    "Config file is badly formatted",
    "Node <config></config> not found",
    "Invalid thread count in <threads></threads>",
    "Task list <tasks></tasks> is empty",
    "Node <target></target> not found",
    "Node <section></section> not found",
    "Invalid target: use 'cpu' or 'gpu'",
    "Invalid executions: must be a positive integer",
    "Invalid task name: cannot be empty",
    "Invalid extra data: cannot be empty",
    "Invalid method: use 'profile' or 'total'",
    "Node <start></start> not found",
    "Node <end></end> not found",
    "Node <cu></cu> not found",
    "Node <line></line> not found",
    "Invalid compilation unit: cannot be empty",
    "Invalid line number: must be a positive integer"
};

// begin helper functions

cfg_expected<config_data::position> get_position(const pugi::xml_node& pos_node)
{
    using namespace pugi;
    xml_node cu = pos_node.child("cu");
    // <cu>...</cu> exists
    if (!cu)
        return cfg_error(cfg_error_code::POS_NO_COMP_UNIT);
    // <cu>...</cu> is not empty
    if (!*cu.child_value())
        return cfg_error(cfg_error_code::POS_INVALID_COMP_UNIT);
    xml_node line = pos_node.child("line");
    // <line>...</line> exists
    if (!line)
        return cfg_error(cfg_error_code::POS_NO_LINE);
    // <line>...</line> is not empty or negative
    int lineno;
    if ((lineno = line.text().as_int(0)) <= 0)
        return cfg_error(cfg_error_code::POS_INVALID_LINE);
    return config_data::position(cu.child_value(), lineno);
}

cfg_expected<config_data::section> get_section(const pugi::xml_node& section)
{
    using namespace pugi;
    // <start>...</start>
    xml_node start = section.child("start");
    if (!start)
        return cfg_error(cfg_error_code::SEC_NO_START_NODE);
    // <end>...</end>
    xml_node end = section.child("end");
    if (!end)
        return cfg_error(cfg_error_code::SEC_NO_END_NODE);

    // position error checks
    cfg_expected<config_data::position> pstart = get_position(start);
    if (!pstart)
        return pstart.error();
    cfg_expected<config_data::position> pend = get_position(end);
    if (!pend)
        return pend.error();

    return config_data::section(std::move(pstart.value()), std::move(pend.value()));
}

cfg_expected<config_data::target> get_target(const pugi::xml_node& target)
{
    const pugi::char_t* tgt_str = target.child_value();
    if (!strcmp(tgt_str, "cpu"))
        return config_data::target::cpu;
    if (!strcmp(tgt_str, "gpu"))
        return config_data::target::gpu;
    return cfg_error(cfg_error_code::TASK_INVALID_TARGET);
}

cfg_expected<config_data::profiling_method> get_method(const pugi::xml_node& method)
{
    const pugi::char_t* method_str = method.child_value();
    if (!strcmp(method_str, "profile"))
        return config_data::profiling_method::energy_profile;
    if (!strcmp(method_str, "total"))
        return config_data::profiling_method::energy_total;
    return cfg_error(cfg_error_code::TASK_INVALID_METHOD);
}

cfg_expected<config_data::task> get_task(const pugi::xml_node& task)
{
    using namespace pugi;
    // <name>...</name> - optional, must not be empty
    xml_node nname = task.child("name");
    if (nname && !*nname.child_value())
        return cfg_error(cfg_error_code::TASK_INVALID_NAME);

    // <target>...</target>
    xml_node ntarget = task.child("target");
    if (!ntarget)
        return cfg_error(cfg_error_code::TASK_NO_TARGET);
    cfg_expected<config_data::target> target = get_target(ntarget);
    if (!target)
        return target.error();

    // <method>...</method> - optional
    // default is 'total'; should have no effect when target is 'gpu' due to the
    // nature of the power/energy reading interface
    xml_node nmethod = task.child("method");
    config_data::profiling_method method = config_data::profiling_method::energy_total;
    if (nmethod)
    {
        cfg_expected<config_data::profiling_method> result = get_method(nmethod);
        if (!result)
            return result.error();
        method = result.value();
    }

    // <section>...</section>
    xml_node nsection = task.child("section");
    if (!nsection)
        return cfg_error(cfg_error_code::TASK_NO_SECTION);
    cfg_expected<config_data::section> section = get_section(nsection);
    if (!section)
        return section.error();

    // <extra>...</extra> - optional, must not be empty
    xml_node nxtra = task.child("extra");
    if (nxtra && !*nxtra.child_value())
        return cfg_error(cfg_error_code::TASK_INVALID_EXTRA);

    // <execs>...</execs> - optional, must be a positive integer
    // if not present - assumed zero
    xml_node nexecs = task.child("execs");
    int execs = 0;
    if (nexecs && (execs = nexecs.text().as_int(0)) <= 0)
        return cfg_error(cfg_error_code::TASK_INVALID_EXECS);

    return config_data::task(
        nname.child_value(),
        nxtra.child_value(),
        target.value(),
        method,
        std::move(section.value()),
        execs);
}

// end helper functions

cfg_expected<config_data> tep::load_config(const char* file)
{
    using namespace pugi;

    config_data cfgdata;
    xml_document doc;
    xml_parse_result parse_result = doc.load_file(file);
    if (!parse_result)
    {
        switch (parse_result.status)
        {
        case status_file_not_found:
            return cfg_error(cfg_error_code::CONFIG_NOT_FOUND);
        case status_io_error:
            return cfg_error(cfg_error_code::CONFIG_IO_ERROR);
        case status_out_of_memory:
            return cfg_error(cfg_error_code::CONFIG_OUT_OF_MEM);
        default:
            return cfg_error(cfg_error_code::CONFIG_BAD_FORMAT);
        }
    }
    // <config>...</config>
    xml_node nconfig = doc.child("config");
    if (!nconfig)
        return cfg_error(cfg_error_code::CONFIG_NO_CONFIG);

    // <threads>...</threads> - optional, must be positive integer
    xml_node nthreads = nconfig.child("threads");
    int threads;
    if (nthreads && (threads = nthreads.text().as_int(0)) <= 0)
        return cfg_error(cfg_error_code::INVALID_THREAD_CNT);
    cfgdata.threads = threads;

    // iterate all tasks
    // <tasks>...</tasks> - optional
    xml_node ntasks = nconfig.child("tasks");
    if (ntasks)
    {
        int task_count = 0;
        for (xml_node ntask = ntasks.first_child(); ntask; ntask = ntask.next_sibling(), task_count++)
        {
            // <task>...</task>
            cfg_expected<config_data::task> task = get_task(ntask);
            if (!task)
                return task.error();
            cfgdata.tasks.push_back(std::move(task.value()));
        }
        if (task_count == 0)
            return cfg_error(cfg_error_code::TASK_LIST_EMPTY);
    }

    return cfgdata;
}

// operator overloads

std::ostream& tep::operator<<(std::ostream& os, const cfg_error& res)
{
    auto idx = static_cast<std::underlying_type_t<cfg_error_code>>(res.code());
    os << error_messages[idx] << " (error code " << idx << ")";
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::position& p)
{
    os << p.compilation_unit << ":" << p.line;
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::section& s)
{
    os << s.start << " - " << s.end;
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::task& t)
{
    os << "name: " << (t.name.empty() ? "-" : t.name);
    os << "\nextra: " << (t.extra.empty() ? "-" : t.extra);
    os << "\ntarget: ";
    switch (t.target)
    {
    case config_data::target::cpu:
        os << "cpu";
        break;
    case config_data::target::gpu:
        os << "gpu";
        break;
    }
    os << "\nmethod: ";
    switch (t.method)
    {
    case config_data::profiling_method::energy_profile:
        os << "profile";
        break;
    case config_data::profiling_method::energy_total:
        os << "total energy consumption";
        break;
    }
    os << "\nsection: " << t.section;
    os << "\nexecutions: " << t.executions;
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data& cd)
{
    os << "threads: " << cd.threads;
    os << "\ntasks:";
    for (const auto& task : cd.tasks)
        os << "\n----------\n" << task;
    return os;
}

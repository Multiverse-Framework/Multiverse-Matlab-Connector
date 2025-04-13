#define S_FUNCTION_NAME multiverse_connector /* Specifies the name of the S-function (timestwo) */
#define S_FUNCTION_LEVEL 2                   /* Specifies that the S-function is in the Level 2 format */

#include "simstruc.h"                        /* Defines the data structure */

#include <string>
#include <multiverse_client_json.h>
#include <map>
#include <set>
#include <vector>

static std::map<std::string, size_t> attribute_map_double = {
    {"", 0},
    {"time", 1},
    {"scalar", 1},
    {"position", 3},
    {"quaternion", 4},
    {"relative_velocity", 6},
    {"odometric_velocity", 6},
    {"joint_rvalue", 1},
    {"joint_tvalue", 1},
    {"joint_linear_velocity", 1},
    {"joint_angular_velocity", 1},
    {"joint_linear_acceleration", 1},
    {"joint_angular_acceleration", 1},
    {"joint_force", 1},
    {"joint_torque", 1},
    {"cmd_joint_rvalue", 1},
    {"cmd_joint_tvalue", 1},
    {"cmd_joint_linear_velocity", 1},
    {"cmd_joint_angular_velocity", 1},
    {"cmd_joint_force", 1},
    {"cmd_joint_torque", 1},
    {"joint_position", 3},
    {"joint_quaternion", 4},
    {"force", 3},
    {"torque", 3}};

static Json::Value string_to_json(const std::string &str)
{
    if (str.empty())
    {
        return Json::Value();
    }
    Json::Value json;
    Json::Reader reader;
    if (reader.parse(str, json) && !str.empty())
    {
        return json;
    }
    else
    {
        mexPrintf("Failed to parse JSON string: %s", str.c_str());
        return Json::Value();
    }
}

class MultiverseConnector : public MultiverseClientJson
{
public:
    MultiverseConnector(
        const std::string &in_host = "tcp://127.0.0.1",
        const std::string &in_server_port = "7000",
        const std::string &in_client_port = "7593",
        const std::string &world_name = "world",
        const std::string &simulation_name = "matlab_connector",
        const Json::Value &param_json = Json::Value())
    {
        meta_data["world_name"] = world_name;
        meta_data["simulation_name"] = simulation_name;
        meta_data["length_unit"] = "m";
        meta_data["angle_unit"] = "rad";
        meta_data["mass_unit"] = "kg";
        meta_data["time_unit"] = "s";
        meta_data["handedness"] = "rhs";

        host = in_host;
        server_port = in_server_port;
        client_port = in_client_port;
        
        if (param_json.isMember("send"))
        {
            for (const std::string &object_name : param_json["send"].getMemberNames())
            {
                send_objects[object_name] = {};
                for (const Json::Value &attribute_name : param_json["send"][object_name])
                {
                    send_objects[object_name].insert(attribute_name.asString());
                }
            }
        }
        if (param_json.isMember("receive"))
        {
            for (const std::string &object_name : param_json["receive"].getMemberNames())
            {
                receive_objects[object_name] = {};
                for (const Json::Value &attribute_name : param_json["receive"][object_name])
                {
                    receive_objects[object_name].insert(attribute_name.asString());
                }
            }
        }
    }

    ~MultiverseConnector()
    {
    }

public:
    void start()
    {
        connect();
        *world_time = 0.0;
        reset();

        communicate(true);
    }

    size_t get_send_data_size() const
    {
        return send_buffer.buffer_double.size;
    }

    size_t get_receive_data_size() const
    {
        return receive_buffer.buffer_double.size;
    }

    void set_send_data_at(size_t index, double value)
    {
        if (index < send_buffer.buffer_double.size)
        {
            send_buffer.buffer_double.data[index] = value;
        }
        else
        {
            mexPrintf("Index out of bounds for send data: %zu\n", index);
        }
    }

    double get_receive_data_at(size_t index) const
    {
        if (index < receive_buffer.buffer_double.size)
        {
            return receive_buffer.buffer_double.data[index];
        }
        else
        {
            mexPrintf("Index out of bounds for receive data: %zu\n", index);
            return 0.0;
        }
    }

    std::map<std::string, std::map<std::string, std::vector<double *>>> get_send_objects_data() const
    {
        return send_objects_data;
    }

    std::map<std::string, std::map<std::string, std::vector<double *>>> get_receive_objects_data() const
    {
        return receive_objects_data;
    }

private:
    void start_connect_to_server_thread() override
    {
        connect_to_server();
    }

    void wait_for_connect_to_server_thread_finish() override
    {
    }

    void start_meta_data_thread() override
    {
        send_and_receive_meta_data();
    }

    void wait_for_meta_data_thread_finish() override
    {
    }

    bool init_objects(bool) override
    {
        return send_objects.size() > 0 || receive_objects.size() > 0;
    }

    void bind_request_meta_data() override
    {
        // Create JSON object and populate it
        request_meta_data_json.clear();
        request_meta_data_json["meta_data"]["world_name"] = meta_data["world_name"];
        request_meta_data_json["meta_data"]["simulation_name"] = meta_data["simulation_name"];
        request_meta_data_json["meta_data"]["length_unit"] = meta_data["length_unit"];
        request_meta_data_json["meta_data"]["angle_unit"] = meta_data["angle_unit"];
        request_meta_data_json["meta_data"]["mass_unit"] = meta_data["mass_unit"];
        request_meta_data_json["meta_data"]["time_unit"] = meta_data["time_unit"];
        request_meta_data_json["meta_data"]["handedness"] = meta_data["handedness"];

        for (const std::pair<const std::string, std::set<std::string>> &send_object : send_objects)
        {
            for (const std::string &attribute_name : send_object.second)
            {
                request_meta_data_json["send"][send_object.first].append(attribute_name);
            }
        }

        for (const std::pair<const std::string, std::set<std::string>> &receive_object : receive_objects)
        {
            for (const std::string &attribute_name : receive_object.second)
            {
                request_meta_data_json["receive"][receive_object.first].append(attribute_name);
            }
        }

        request_meta_data_str = request_meta_data_json.toStyledString();

        mexPrintf("Send RequestMetaData: %s\n", request_meta_data_str.c_str());
    }

    void bind_response_meta_data() override
    {
        mexPrintf("Receive ResponseMetaData: %s\n", response_meta_data_str.c_str());

        send_objects.clear();
        for (const std::string &object_name : response_meta_data_json["send"].getMemberNames())
        {
            send_objects[object_name] = {};
            for (const std::string &attribute_name : response_meta_data_json["send"][object_name].getMemberNames())
            {
                send_objects[object_name].insert(attribute_name);
            }
        }

        receive_objects.clear();
        for (const std::string &object_name : response_meta_data_json["receive"].getMemberNames())
        {
            receive_objects[object_name] = {};
            for (const std::string &attribute_name : response_meta_data_json["receive"][object_name].getMemberNames())
            {
                receive_objects[object_name].insert(attribute_name);
            }
        }
    }

    void bind_api_callbacks() override
    {
    }

    void bind_api_callbacks_response() override
    {
    }

    void init_send_and_receive_data() override
    {
        double *send_buffer_double = send_buffer.buffer_double.data;
        for (const std::pair<const std::string, std::set<std::string>> &send_object : send_objects)
        {
            send_objects_data[send_object.first] = {};
            for (const std::string &attribute_name : send_object.second)
            {
                send_objects_data[send_object.first][attribute_name] = {};
                for (size_t i = 0; i < attribute_map_double[attribute_name]; ++i)
                {
                    send_objects_data[send_object.first][attribute_name].emplace_back(send_buffer_double++);
                }
            }
        }

        double *receive_buffer_double = receive_buffer.buffer_double.data;
        for (const std::pair<const std::string, std::set<std::string>> &receive_object : receive_objects)
        {
            receive_objects_data[receive_object.first] = {};
            for (const std::string &attribute_name : receive_object.second)
            {
                receive_objects_data[receive_object.first][attribute_name] = {};
                for (size_t i = 0; i < attribute_map_double[attribute_name]; ++i)
                {
                    receive_objects_data[receive_object.first][attribute_name].emplace_back(receive_buffer_double++);
                }
            }
        }
    }

    void bind_send_data() override
    {
        *world_time = get_time_now() - sim_start_time;
    }

    void bind_receive_data() override
    {
    }

    void clean_up() override
    {
        send_objects_data.clear();
        receive_objects_data.clear();
    }

    void reset() override
    {
        sim_start_time = get_time_now();
    }

private:
    std::map<std::string, std::string> meta_data;

    std::map<std::string, std::set<std::string>> send_objects;

    std::map<std::string, std::set<std::string>> receive_objects;

    std::map<std::string, std::map<std::string, std::vector<double *>>> send_objects_data;

    std::map<std::string, std::map<std::string, std::vector<double *>>> receive_objects_data;

    double sim_start_time;
};

static std::string host_str = "";
static std::string server_port_str = "";
static std::string client_port_str = "";
static std::string world_name_str = "";
static std::string simulation_name_str = "";
static Json::Value param_json = Json::Value();

static MultiverseConnector *mc = nullptr;

static void mdlInitializeSizes(SimStruct *S) /* Initialize the input and output ports and their size */
{
    if (mc != nullptr)
    {
        mexPrintf("MultiverseConnector already initialized, this should not happen.\n");
    }
    ssSetNumSFcnParams(S, 6);
    if (ssGetNumSFcnParams(S) != ssGetSFcnParamsCount(S))
    {
        ssSetErrorStatus(S, "Incorrect number of parameters passed to S-function, expected 6.");
        return;
    }

    const mxArray *host = ssGetSFcnParam(S, 0);
    if (!mxIsChar(host))
    {
        ssSetErrorStatus(S, "Host must be a string.");
        return;
    }
    host_str = mxArrayToString(host);
    if (host_str.empty())
    {
        ssSetErrorStatus(S, "Host string cannot be empty.");
        return;
    }

    const mxArray *server_port = ssGetSFcnParam(S, 1);
    if (!mxIsChar(server_port))
    {
        ssSetErrorStatus(S, "Server port must be a string.");
        return;
    }
    server_port_str = mxArrayToString(server_port);
    if (server_port_str.empty())
    {
        ssSetErrorStatus(S, "Server port string cannot be empty.");
        return;
    }

    const mxArray *client_port = ssGetSFcnParam(S, 2);
    if (!mxIsChar(client_port))
    {
        ssSetErrorStatus(S, "Client port must be a string.");
        return;
    }
    client_port_str = mxArrayToString(client_port);
    if (client_port_str.empty())
    {
        ssSetErrorStatus(S, "Client port string cannot be empty.");
        return;
    }

    const mxArray *world_name = ssGetSFcnParam(S, 3);
    if (!mxIsChar(world_name))
    {
        ssSetErrorStatus(S, "World name must be a string.");
        return;
    }
    world_name_str = mxArrayToString(world_name);
    if (world_name_str.empty())
    {
        ssSetErrorStatus(S, "World name string cannot be empty.");
        return;
    }

    const mxArray *simulation_name = ssGetSFcnParam(S, 4);
    if (!mxIsChar(simulation_name))
    {
        ssSetErrorStatus(S, "Simulation name must be a string.");
        return;
    }
    simulation_name_str = mxArrayToString(simulation_name);
    if (simulation_name_str.empty())
    {
        ssSetErrorStatus(S, "Simulation name string cannot be empty.");
        return;
    }

    const mxArray *param = ssGetSFcnParam(S, 5);
    if (!mxIsChar(param))
    {
        ssSetErrorStatus(S, "Parameter must be a string.");
        return;
    }

    const std::string param_str = mxArrayToString(param);
    if (param_str.empty())
    {
        ssSetErrorStatus(S, "Parameter string cannot be empty.");
        return;
    }
    param_json = string_to_json(param_str);

    mc = new MultiverseConnector(
        host_str,
        server_port_str,
        client_port_str,
        world_name_str,
        simulation_name_str,
        param_json);
    mc->start();

    if (!ssSetNumInputPorts(S, 1))
        return;
    ssSetInputPortWidth(S, 0, mc->get_send_data_size());
    ssSetInputPortDirectFeedThrough(S, 0, 1);

    if (!ssSetNumOutputPorts(S, 1))
        return;
    ssSetOutputPortWidth(S, 0, mc->get_receive_data_size());

    ssSetNumSampleTimes(S, 1);

    /* Take care when specifying exception free code - see sfuntmpl.doc */
    ssSetOptions(S, SS_OPTION_EXCEPTION_FREE_CODE);
}
static void mdlInitializeSampleTimes(SimStruct *S) /* Set the sample time of the S-function as inherited */
{
    ssSetSampleTime(S, 0, INHERITED_SAMPLE_TIME);
    ssSetOffsetTime(S, 0, 0.0);
}
static void mdlOutputs(SimStruct *S, int_T tid) /* Calculate the block output for each time step */
{
    int_T i;
    InputRealPtrsType input_ptrs = ssGetInputPortRealSignalPtrs(S, 0);
    for (i = 0; i < mc->get_send_data_size(); i++)
    {
        mc->set_send_data_at(i, *input_ptrs[i]);
    }

    mc->communicate();

    real_T *output_ptrs = ssGetOutputPortRealSignal(S, 0);
    for (i = 0; i < mc->get_receive_data_size(); i++)
    {
        output_ptrs[i] = mc->get_receive_data_at(i);
    }
}
static void mdlTerminate(SimStruct *S) {
    if (mc != nullptr)
    {
        mexPrintf("Terminating MultiverseConnector...\n");
        delete mc;
        mc = nullptr;
        mexPrintf("MultiverseConnector terminated.\n");
    }
} /* Perform tasks at the end of the simulation */

#ifdef MATLAB_MEX_FILE /* Is this file being compiled as a MEX-file? */
#include "simulink.c"  /* MEX-file interface mechanism */
#else
#include "cg_sfun.h" /* Code generation registration function */
#endif
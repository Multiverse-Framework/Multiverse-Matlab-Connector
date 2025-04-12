#include <string>
#include <multiverse_client_json.h>
#include <map>
#include <set>
#include <vector>

static std::map<std::string, size_t> attribute_map_double = {
    {"", 0},
    {"time", 1},
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

class MultiverseConnector : public MultiverseClientJson
{
public:
    MultiverseConnector(
        const std::string &world_name = "world",
        const std::string &simulation_name = "matlab_connector",
        const std::string &in_host = "tcp://127.0.0.1",
        const std::string &in_server_port = "7000",
        const std::string &in_client_port = "7593")
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

        // receive_objects = {
        //     {"", {"position", "quaternion"}}};

        connect();

        *world_time = 0.0;

        reset();
    }

    ~MultiverseConnector()
    {
    }

public:
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
    }

    void bind_response_meta_data() override
    {
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

#define S_FUNCTION_NAME multiverse_connector    /* Specifies the name of the S-function (timestwo) */
#define S_FUNCTION_LEVEL 2                      /* Specifies that the S-function is in the Level 2 format */

#include "simstruc.h"                           /* Defines the data structure */
static void mdlInitializeSizes(SimStruct *S)    /* Initialize the input and output ports and their size */
{
    ssSetNumSFcnParams(S, 1);
    if (ssGetNumSFcnParams(S) != ssGetSFcnParamsCount(S))
    {
        ssSetErrorStatus(S, "Incorrect number of parameters passed to S-function, expected 1.");
        return;
    }

    const mxArray* param = ssGetSFcnParam(S, 0);
    if (!mxIsChar(param))
    {
        ssSetErrorStatus(S, "Parameter must be a string.");
        return;
    }

    const std::string paramStr = mxArrayToString(param);
    if (paramStr.empty())
    {
        ssSetErrorStatus(S, "Parameter string cannot be empty.");
        return;
    }

    // Print information about the parameter
    mexPrintf("Parameter: %s\n", paramStr.c_str());

    MultiverseConnector *client = new MultiverseConnector();

    // Json::CharReaderBuilder builder;
    // Json::Value root;
    // std::string errs;

    if (!ssSetNumInputPorts(S, 1))
        return;
    ssSetInputPortWidth(S, 0, DYNAMICALLY_SIZED);
    ssSetInputPortDirectFeedThrough(S, 0, 1);

    if (!ssSetNumOutputPorts(S, 1))
        return;
    ssSetOutputPortWidth(S, 0, DYNAMICALLY_SIZED);

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
    InputRealPtrsType uPtrs = ssGetInputPortRealSignalPtrs(S, 0);
    real_T *y = ssGetOutputPortRealSignal(S, 0);
    int_T width = ssGetOutputPortWidth(S, 0);

    for (i = 0; i < width; i++)
    {
        *y++ = 5.0 * (*uPtrs[i]);
    }
}
static void mdlTerminate(SimStruct *S) {} /* Perform tasks at the end of the simulation */

#ifdef MATLAB_MEX_FILE /* Is this file being compiled as a MEX-file? */
#include "simulink.c"  /* MEX-file interface mechanism */
#else
#include "cg_sfun.h" /* Code generation registration function */
#endif
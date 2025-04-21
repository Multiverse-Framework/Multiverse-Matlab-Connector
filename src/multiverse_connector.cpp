#define S_FUNCTION_NAME multiverse_connector /* Specifies the name of the S-function (timestwo) */
#define S_FUNCTION_LEVEL 2                   /* Specifies that the S-function is in the Level 2 format */

#include "simstruc.h" /* Defines the data structure */

#include <regex>
#include <string>
#include <iterator>
#include <fstream>
#include <string>
#include <multiverse_client_json.h>
#include <map>
#include <set>
#include <vector>
#include <thread>

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
        const Json::Value &param_json = Json::Value(),
        const double in_time_step = 0.001)
    {
        meta_data["world_name"] = world_name;
        meta_data["simulation_name"] = simulation_name;
        meta_data["length_unit"] = "m";
        meta_data["angle_unit"] = "rad";
        meta_data["mass_unit"] = "kg";
        meta_data["time_unit"] = "s";
        meta_data["handedness"] = "rhs";
        time_step = in_time_step;

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
        if (param_json.isMember("api_callbacks"))
        {
            api_callbacks = param_json["api_callbacks"];
        }
        else
        {
            api_callbacks = Json::Value();
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
        mexPrintf("Send RequestMetaData: %s\n", request_meta_data_str.c_str());
        mexPrintf("Receive ResponseMetaData: %s\n", response_meta_data_str.c_str());
        communicate(false);
        communicate_thread = new std::thread([this]()
                                             { 
                                              while (!should_stop)
                                              {
                                                const double time_now = get_time_now();
                                                if (!api_callbacks.empty())
                                                {
                                                    request_meta_data_json["api_callbacks"] = api_callbacks;
                                                    request_meta_data_str = request_meta_data_json.toStyledString();
                                                    communicate(true);
                                                }
                                                communicate(false);
                                                const double time_diff = get_time_now() - time_now;
                                                if (time_diff < time_step)
                                                {
                                                    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>((time_step - time_diff) * 1000)));
                                                }
                                              } });
    }

    void stop()
    {
        if (communicate_thread != nullptr)
        {
            should_stop = true;
            communicate_thread->join();
            delete communicate_thread;
            communicate_thread = nullptr;
        }
    }

    void set_sim_time(const double time)
    {
        sim_time = time;
    }

    double get_world_time() const
    {
        return *world_time;
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

    void set_api_callbacks(const Json::Value &in_api_callbacks)
    {
        // mexPrintf("Set API callbacks: %s\n", in_api_callbacks.toStyledString().c_str());
        api_callbacks = in_api_callbacks;
    }

    Json::Value get_api_callbacks_response() const
    {
        return api_callbacks_response;
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
        return true;
    }

    void bind_request_meta_data() override
    {
        // Create JSON object and populate it
        if (!request_meta_data_json.isMember("api_callbacks"))
        {
            request_meta_data_json.clear();
        }

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

        if (response_meta_data_json.isMember("api_callbacks_response"))
        {
            api_callbacks_response = response_meta_data_json["api_callbacks_response"];
        }
        else
        {
            api_callbacks_response = Json::Value();
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
        *world_time = sim_time;
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
        sim_time = 0.0;
    }

private:
    std::map<std::string, std::string> meta_data;

    std::map<std::string, std::set<std::string>> send_objects;

    std::map<std::string, std::set<std::string>> receive_objects;

    std::map<std::string, std::map<std::string, std::vector<double *>>> send_objects_data;

    std::map<std::string, std::map<std::string, std::vector<double *>>> receive_objects_data;

    Json::Value api_callbacks;

    Json::Value api_callbacks_response;

    std::thread *communicate_thread = nullptr;

    bool should_stop = false;

    double sim_time;

    double time_step = 0.001;
};

static void mdlInitializeSizes(SimStruct *S) /* Initialize the input and output ports and their size */
{
    ssSetNumSFcnParams(S, 7);
    if (ssGetNumSFcnParams(S) != ssGetSFcnParamsCount(S))
    {
        ssSetErrorStatus(S, "Incorrect number of parameters passed to S-function, expected 7.");
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
    const Json::Value param_json = string_to_json(param_str);

    int input_port_size = 1;
    if (param_json.isMember("send"))
    {
        for (const std::string &object_name : param_json["send"].getMemberNames())
        {
            for (const Json::Value &attribute_name : param_json["send"][object_name])
            {
                if (attribute_map_double.find(attribute_name.asString()) == attribute_map_double.end())
                {
                    const std::string error_message = "Attribute: " + attribute_name.asString() + " not found in attribute_map_double.";
                    ssSetErrorStatus(S, error_message.c_str());
                    return;
                }
                input_port_size += attribute_map_double[attribute_name.asString()];
            }
        }
    }
    else
    {
        mexPrintf("send not found\n");
    }
    mexPrintf("Input port size: %d\n", input_port_size);

    int output_port_size = 1;
    if (param_json.isMember("receive"))
    {
        for (const std::string &object_name : param_json["receive"].getMemberNames())
        {
            for (const Json::Value &attribute_name : param_json["receive"][object_name])
            {
                if (attribute_map_double.find(attribute_name.asString()) == attribute_map_double.end())
                {
                    const std::string error_message = "Attribute: " + attribute_name.asString() + " not found in attribute_map_double.";
                    ssSetErrorStatus(S, error_message.c_str());
                    return;
                }
                output_port_size += attribute_map_double[attribute_name.asString()];
            }
        }
    }
    else
    {
        mexPrintf("receive not found\n");
    }
    mexPrintf("Output port size: %d\n", output_port_size);

    if (!ssSetNumInputPorts(S, 1))
        return;
    ssSetInputPortWidth(S, 0, input_port_size);
    ssSetInputPortDirectFeedThrough(S, 0, 1);
    ssSetInputPortDataType(S, 0, SS_DOUBLE);

    if (!ssSetNumOutputPorts(S, 2))
        return;
    ssSetOutputPortWidth(S, 0, output_port_size);
    ssSetOutputPortWidth(S, 1, 10 * 10000);

    ssSetNumSampleTimes(S, 1);

    /* Take care when specifying exception free code - see sfuntmpl.doc */
    ssSetOptions(S, SS_OPTION_EXCEPTION_FREE_CODE);

    ssSetNumPWork(S, 1);
}
#define MDL_START
static void mdlStart(SimStruct *S)
{
    const mxArray *host = ssGetSFcnParam(S, 0);
    if (!mxIsChar(host))
    {
        ssSetErrorStatus(S, "Host must be a string.");
        return;
    }
    const std::string host_str = mxArrayToString(host);
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
    const std::string server_port_str = mxArrayToString(server_port);
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
    const std::string client_port_str = mxArrayToString(client_port);
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
    const std::string world_name_str = mxArrayToString(world_name);
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
    const std::string simulation_name_str = mxArrayToString(simulation_name);
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
    const Json::Value param_json = string_to_json(param_str);

    const mxArray *time_step = ssGetSFcnParam(S, 6);
    if (!mxIsDouble(time_step) || mxGetNumberOfElements(time_step) != 1)
    {
        ssSetErrorStatus(S, "Step time must be a double.");
        return;
    }
    if (mxGetPr(time_step)[0] <= 0)
    {
        ssSetErrorStatus(S, "Step time must be positive.");
        return;
    }
    const double time_step_value = mxGetPr(time_step)[0];

    MultiverseConnector *mc = new MultiverseConnector(
        host_str,
        server_port_str,
        client_port_str,
        world_name_str,
        simulation_name_str,
        param_json,
        time_step_value);

    mc->start();

    // Save in work state
    ssSetPWorkValue(S, 0, mc);
}
static void mdlInitializeSampleTimes(SimStruct *S) /* Set the sample time of the S-function as inherited */
{
    ssSetSampleTime(S, 0, INHERITED_SAMPLE_TIME);
    ssSetOffsetTime(S, 0, 0.0);
}
static void mdlOutputs(SimStruct *S, int_T tid) /* Calculate the block output for each time step */
{
    MultiverseConnector *mc = static_cast<MultiverseConnector *>(ssGetPWorkValue(S, 0));
    if (mc == nullptr)
    {
        ssSetErrorStatus(S, "MultiverseConnector is null !!!");
        return;
    }
    int_T i;
    InputRealPtrsType input_ptrs = ssGetInputPortRealSignalPtrs(S, 0);
    mc->set_sim_time(*input_ptrs[0]);
    for (i = 0; i < mc->get_send_data_size(); i++)
    {
        mc->set_send_data_at(i, *input_ptrs[i + 1]);
    }

    real_T *output_1_ptrs = ssGetOutputPortRealSignal(S, 0);
    output_1_ptrs[0] = mc->get_world_time();
    for (i = 0; i < mc->get_receive_data_size(); i++)
    {
        output_1_ptrs[i + 1] = mc->get_receive_data_at(i);
    }

    const Json::Value api_callbacks_response = mc->get_api_callbacks_response();
    if (!api_callbacks_response.empty())
    {
        for (const std::string &simulation_name : api_callbacks_response.getMemberNames())
        {
            for (const Json::Value simulation_api_callback_response : api_callbacks_response[simulation_name])
            {
                for (const std::string &function_name : simulation_api_callback_response.getMemberNames())
                {
                    const Json::Value function_response = simulation_api_callback_response[function_name];
                    // mexPrintf("Simulation: %s, Function: %s, Response: %s\n", simulation_name.c_str(), function_name.c_str(), function_response.toStyledString().c_str());
                    if (function_name == "get_everything")
                    {
                        const std::string input = function_response.toStyledString();

                        std::ofstream file("./output.txt");  // open file for writing
                        if (file.is_open()) {
                            file << input;
                            file.close();  // always close when done
                        } else {
                            mexPrintf("Unable to open file for writing\n");
                        }

                        real_T *output_2_ptrs = ssGetOutputPortRealSignal(S, 1);
                        const int_T output_2_size = ssGetOutputPortWidth(S, 1);

                        // Step 1: Find the last two double quotes
                        size_t last_quote = input.rfind('"');
                        if (last_quote == std::string::npos) return;

                        size_t second_last_quote = input.rfind('"', last_quote - 1);
                        if (second_last_quote == std::string::npos) return;

                        // Step 2: Extract content between them
                        std::string data = input.substr(second_last_quote + 1, last_quote - second_last_quote - 1);

                        // Step 3: Parse all numbers inside
                        std::regex number_regex(R"(-?\d+(\.\d+)?([eE][-+]?\d+)?)");

                        int idx = 0;
                        for (std::sregex_iterator it(data.begin(), data.end(), number_regex);
                            it != std::sregex_iterator(); ++it)
                        {
                            if (idx >= output_2_size)
                            {
                                mexPrintf("Output 2 size exceeded: %d\n", output_2_size);
                                break;
                            }
                            output_2_ptrs[idx++] = std::stod(it->str());
                        }
                    }
                }
            }
        }
    }
}
static void mdlTerminate(SimStruct *S)
{
    MultiverseConnector *mc = static_cast<MultiverseConnector *>(ssGetPWorkValue(S, 0));
    if (mc != nullptr)
    {
        mexPrintf("Terminating MultiverseConnector...\n");
        mc->stop();
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
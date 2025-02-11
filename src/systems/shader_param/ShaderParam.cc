/*
 * Copyright (C) 2022 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "ShaderParam.hh"

#include <chrono>
#include <list>
#include <map>
#include <mutex>
#include <vector>
#include <string>

#include <ignition/common/Profiler.hh>
#include <ignition/plugin/Register.hh>
#include <ignition/rendering/Material.hh>
#include <ignition/rendering/RenderingIface.hh>
#include <ignition/rendering/Scene.hh>
#include <ignition/rendering/ShaderParams.hh>
#include <ignition/rendering/Visual.hh>

#include <sdf/Element.hh>

#include "ignition/gazebo/components/Name.hh"
#include "ignition/gazebo/components/SourceFilePath.hh"
#include "ignition/gazebo/rendering/Events.hh"
#include "ignition/gazebo/rendering/RenderUtil.hh"
#include "ignition/gazebo/Util.hh"

using namespace ignition;
using namespace gazebo;
using namespace systems;

class ignition::gazebo::systems::ShaderParamPrivate
{
  /// \brief Data structure for storing shader param info
  public: class ShaderParamValue
  {
    /// \brief shader type: vertex or fragment
    public: std::string shader;

    /// \brief variable type: int, float, float_array, int_array,
    /// texture, texture_cube
    public: std::string type;

    /// \brief variable name of param
    public: std::string name;

    /// \brief param value
    public: std::string value;

    /// \brief Any additional arguments
    public: std::vector<std::string> args;
  };

  /// \brief Data structure for storing shader files uri
  public: class ShaderUri
  {
    /// \brief Shader language: glsl or metal
    public: std::string language;

    /// \brief Path to vertex shader
    public: std::string vertexShaderUri;

    /// \brief Path to fragment shader
    public: std::string fragmentShaderUri;
  };

  /// \brief A map of shader language to shader program files
  public: std::map<std::string, ShaderUri> shaders;

  /// \brief Mutex to protect sim time updates.
  public: std::mutex mutex;

  /// \brief Connection to pre-render event callback
  public: ignition::common::ConnectionPtr connection{nullptr};

  /// \brief Name of visual this plugin is attached to
  public: std::string visualName;

  /// \brief Pointer to visual
  public: rendering::VisualPtr visual;

  /// \brief Material used by this visual
  public: rendering::MaterialPtr material;

  /// \brief Pointer to scene
  public: rendering::ScenePtr scene;

  /// \brief Entity id of the visual
  public: Entity entity = kNullEntity;

  /// \brief A list of shader params
  public: std::vector<ShaderParamValue> shaderParams;

  /// \brief Time params that will be updated every iteration
  public: std::vector<ShaderParamValue> timeParams;

  /// \brief Current sim time
  public: std::chrono::steady_clock::duration currentSimTime;

  /// \brief Path to model
  public: std::string modelPath;

  /// \brief All rendering operations must happen within this call
  public: void OnUpdate();
};

/////////////////////////////////////////////////
ShaderParam::ShaderParam()
    : System(), dataPtr(std::make_unique<ShaderParamPrivate>())
{
}

/////////////////////////////////////////////////
ShaderParam::~ShaderParam()
{
}

/////////////////////////////////////////////////
void ShaderParam::Configure(const Entity &_entity,
               const std::shared_ptr<const sdf::Element> &_sdf,
               EntityComponentManager &_ecm,
               EventManager &_eventMgr)
{
  IGN_PROFILE("ShaderParam::Configure");
  // Ugly, but needed because the sdf::Element::GetElement is not a const
  // function and _sdf is a const shared pointer to a const sdf::Element.
  auto sdf = const_cast<sdf::Element *>(_sdf.get());

  if (sdf->HasElement("param"))
  {
    // loop and parse all shader params
    sdf::ElementPtr paramElem = sdf->GetElement("param");
    while (paramElem)
    {
      if (!paramElem->HasElement("shader") ||
          !paramElem->HasElement("name"))
      {
        ignerr << "<param> must have <shader> and <name> sdf elements"
               << std::endl;
        paramElem = paramElem->GetNextElement("param");
        continue;
      }
      std::string shaderType = paramElem->Get<std::string>("shader");
      std::string paramName = paramElem->Get<std::string>("name");

      std::string type = paramElem->Get<std::string>("type", "float").first;
      std::string value = paramElem->Get<std::string>("value", "").first;

      ShaderParamPrivate::ShaderParamValue spv;
      spv.shader = shaderType;
      spv.name = paramName;
      spv.value = value;
      spv.type = type;

      if (paramElem->HasElement("arg"))
      {
        sdf::ElementPtr argElem = paramElem->GetElement("arg");
        while (argElem)
        {
          spv.args.push_back(argElem->Get<std::string>());
          argElem = argElem->GetNextElement("arg");
        }
      }

      this->dataPtr->shaderParams.push_back(spv);
      paramElem = paramElem->GetNextElement("param");
    }
  }

  if (this->dataPtr->modelPath.empty())
  {
    auto modelEntity = topLevelModel(_entity, _ecm);
    this->dataPtr->modelPath =
        _ecm.ComponentData<components::SourceFilePath>(modelEntity).value();
  }

  // parse path to shaders
  if (!sdf->HasElement("shader"))
  {
    ignerr << "Unable to load shader param system. "
           << "Missing <shader> SDF element." << std::endl;
    return;
  }
  // allow mulitple shader SDF element for different shader languages
  sdf::ElementPtr shaderElem = sdf->GetElement("shader");
  while (shaderElem)
  {
    if (!shaderElem->HasElement("vertex") ||
        !shaderElem->HasElement("fragment"))
    {
      ignerr << "<shader> must have <vertex> and <fragment> sdf elements"
             << std::endl;
    }
    else
    {
      // default to glsl
      std::string api = "glsl";
      if (shaderElem->HasAttribute("language"))
        api = shaderElem->GetAttribute("language")->GetAsString();

      ShaderParamPrivate::ShaderUri shader;
      shader.language = api;

      sdf::ElementPtr vertexElem = shaderElem->GetElement("vertex");
      shader.vertexShaderUri = common::findFile(
          asFullPath(vertexElem->Get<std::string>(),
          this->dataPtr->modelPath));
      sdf::ElementPtr fragmentElem = shaderElem->GetElement("fragment");
      shader.fragmentShaderUri = common::findFile(
          asFullPath(fragmentElem->Get<std::string>(),
          this->dataPtr->modelPath));
      this->dataPtr->shaders[api] = shader;
      shaderElem = shaderElem->GetNextElement("shader");
    }
  }
  if (this->dataPtr->shaders.empty())
  {
    ignerr << "Unable to load shader param system. "
           << "No valid shaders." << std::endl;
    return;
  }

  this->dataPtr->entity = _entity;
  auto nameComp = _ecm.Component<components::Name>(_entity);
  this->dataPtr->visualName = nameComp->Data();

  // connect to the SceneUpdate event
  // the callback is executed in the rendering thread so do all
  // rendering operations in that thread
  this->dataPtr->connection =
      _eventMgr.Connect<ignition::gazebo::events::SceneUpdate>(
      std::bind(&ShaderParamPrivate::OnUpdate, this->dataPtr.get()));
}

//////////////////////////////////////////////////
void ShaderParam::PreUpdate(
  const ignition::gazebo::UpdateInfo &_info,
  ignition::gazebo::EntityComponentManager &)
{
  IGN_PROFILE("ShaderParam::PreUpdate");
  std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
  this->dataPtr->currentSimTime = _info.simTime;
}

//////////////////////////////////////////////////
void ShaderParamPrivate::OnUpdate()
{
  std::lock_guard<std::mutex> lock(this->mutex);
  if (this->visualName.empty())
    return;

  if (!this->scene)
    this->scene = rendering::sceneFromFirstRenderEngine();

  if (!this->scene)
    return;

  if (!this->visual)
  {
    // this does a breadth first search for visual with the entity id
    // \todo(anyone) provide a helper function in RenderUtil to search for
    // visual by entity id?
    auto rootVis = scene->RootVisual();
    std::list<rendering::NodePtr> nodes;
    nodes.push_back(rootVis);
    while (!nodes.empty())
    {
      auto n = nodes.front();
      nodes.pop_front();
      if (n && n->HasUserData("gazebo-entity"))
      {
        // RenderUti stores gazebo-entity user data as int
        // \todo(anyone) Change this to uint64_t in Ignition H?
        auto variant = n->UserData("gazebo-entity");
        const int *value = std::get_if<int>(&variant);
        if (value && *value == static_cast<int>(this->entity))
        {
          this->visual = std::dynamic_pointer_cast<rendering::Visual>(n);
          break;
        }
      }
      for (unsigned int i = 0; i < n->ChildCount(); ++i)
        nodes.push_back(n->ChildByIndex(i));
    }
  }

  if (!this->visual)
    return;

  // get the material and set shaders
  if (!this->material)
  {
    auto mat = scene->CreateMaterial();

    // default to glsl
    auto it = this->shaders.find("glsl");
    if (it != this->shaders.end())
    {
      mat->SetVertexShader(it->second.vertexShaderUri);
      mat->SetFragmentShader(it->second.fragmentShaderUri);
    }
    // prefer metal over glsl on macOS
    // \todo(anyone) instead of using ifdef to check for macOS,
    // expose add an accessor function to get the GraphicsApi
    // from rendering::RenderEngine
#ifdef __APPLE__
    auto metalIt = this->shaders.find("metal");
    if (metalIt != this->shaders.end())
    {
      mat->SetVertexShader(metalIt->second.vertexShaderUri);
      mat->SetFragmentShader(metalIt->second.fragmentShaderUri);
      // if both glsl and metal are specified, print a msg to inform that
      // metal is used instead of glsl
      if (it != this->shaders.end())
        ignmsg << "Using metal shaders. " << std::endl;
    }
#endif

    // inherit cast shadows property from existing material
    rendering::MaterialPtr oldMat;
    if (visual->GeometryCount() >  0u)
      oldMat = this->visual->GeometryByIndex(0u)->Material();
    else
      oldMat = this->visual->Material();
    if (oldMat)
      mat->SetCastShadows(oldMat->CastShadows());

    this->visual->SetMaterial(mat);
    scene->DestroyMaterial(mat);
    this->material = this->visual->Material();
  }

  if (!this->material)
    return;

  // set the shader params read from SDF
  // this is only done once
  for (const auto & spv : this->shaderParams)
  {
    // TIME is reserved keyword for sim time
    if (spv.value == "TIME")
    {
      this->timeParams.push_back(spv);
      continue;
    }

    rendering::ShaderParamsPtr params;
    if (spv.shader == "fragment")
    {
      params = this->material->FragmentShaderParams();
    }
    else if (spv.shader == "vertex")
    {
      params = this->material->VertexShaderParams();
    }

    // if no <value> is specified, this could be a constant
    if (spv.value.empty())
    {
      // \todo handle args for constants in ign-rendering
      (*params)[spv.name] = 1;
      continue;
    }

    // handle texture params
    if (spv.type == "texture")
    {
      unsigned int uvSetIndex = spv.args.empty() ? 0u :
          static_cast<unsigned int>(std::stoul(spv.args[0]));
      std::string texPath = common::findFile(
          asFullPath(spv.value, this->modelPath));
      (*params)[spv.name].SetTexture(texPath,
          rendering::ShaderParam::ParamType::PARAM_TEXTURE, uvSetIndex);
    }
    else if (spv.type == "texture_cube")
    {
      unsigned int uvSetIndex = spv.args.empty() ? 0u :
          static_cast<unsigned int>(std::stoul(spv.args[0]));
      std::string texPath = common::findFile(
          asFullPath(spv.value, this->modelPath));
      (*params)[spv.name].SetTexture(texPath,
          rendering::ShaderParam::ParamType::PARAM_TEXTURE_CUBE, uvSetIndex);
    }
    // handle int, float, int_array, and float_array params
    else
    {
      std::vector<std::string> values = common::split(spv.value, " ");

      int intValue = 0;
      float floatValue = 0;
      std::vector<float> floatArrayValue;

      rendering::ShaderParam::ParamType paramType =
          rendering::ShaderParam::PARAM_NONE;

      // float / int
      if (values.size() == 1u)
      {
        std::string str = values[0];

        // if <type> is not empty, respect the specified type
        if (!spv.type.empty())
        {
          if (spv.type == "int")
          {
            intValue = std::stoi(str);
            paramType = rendering::ShaderParam::PARAM_INT;
          }
          else if (spv.type == "float")
          {
            floatValue = std::stof(str);
            paramType = rendering::ShaderParam::PARAM_FLOAT;
          }
        }
        // else do our best guess at what the type is
        else
        {
          std::string::size_type sz;
          int n = std::stoi(str, &sz);
          if ( sz == str.size())
          {
            intValue = n;
            paramType = rendering::ShaderParam::PARAM_INT;
          }
          else
          {
            floatValue = std::stof(str);
            paramType = rendering::ShaderParam::PARAM_FLOAT;
          }
        }
      }
      // arrays
      else
      {
        // int array
        if (!spv.type.empty() && spv.type == "int_array")
        {
          for (const auto &v : values)
            floatArrayValue.push_back(std::stoi(v));
          paramType = rendering::ShaderParam::PARAM_INT_BUFFER;
        }
        // treat everything else as float_array
        else
        {
          for (const auto &v : values)
            floatArrayValue.push_back(std::stof(v));
          paramType = rendering::ShaderParam::PARAM_FLOAT_BUFFER;
        }
      }

      // set the params
      if (paramType == rendering::ShaderParam::PARAM_INT)
      {
        (*params)[spv.name] = intValue;
      }
      else if (paramType == rendering::ShaderParam::PARAM_FLOAT)
      {
        (*params)[spv.name] = floatValue;
      }
      else if (paramType == rendering::ShaderParam::PARAM_INT_BUFFER ||
          paramType == rendering::ShaderParam::PARAM_FLOAT_BUFFER)
      {
        (*params)[spv.name].InitializeBuffer(floatArrayValue.size());
        float *fv = &floatArrayValue[0];
        (*params)[spv.name].UpdateBuffer(fv);
      }
    }
  }
  this->shaderParams.clear();

  // time variables need to be updated every iteration
  for (const auto & spv : this->timeParams)
  {
    float floatValue = (std::chrono::duration_cast<std::chrono::nanoseconds>(
        this->currentSimTime).count()) * 1e-9;
    rendering::ShaderParamsPtr params;
    if (spv.shader == "fragment")
      params = this->material->FragmentShaderParams();
    else if (spv.shader == "vertex")
      params = this->material->VertexShaderParams();
    (*params)[spv.name] = floatValue;
  }
}

IGNITION_ADD_PLUGIN(ShaderParam,
                    ignition::gazebo::System,
                    ShaderParam::ISystemConfigure,
                    ShaderParam::ISystemPreUpdate)

IGNITION_ADD_PLUGIN_ALIAS(ShaderParam,
  "ignition::gazebo::systems::ShaderParam")

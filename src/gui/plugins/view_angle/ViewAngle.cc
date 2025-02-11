/*
 * Copyright (C) 2020 Open Source Robotics Foundation
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

#include "ViewAngle.hh"

#include <ignition/msgs/boolean.pb.h>
#include <ignition/msgs/gui_camera.pb.h>
#include <ignition/msgs/vector3d.pb.h>

#include <iostream>
#include <string>
#include <vector>

#include <ignition/common/Console.hh>
#include <ignition/gui/Application.hh>
#include <ignition/gui/GuiEvents.hh>
#include <ignition/gui/MainWindow.hh>
#include <ignition/math/Angle.hh>
#include <ignition/plugin/Register.hh>
#include <ignition/rendering/MoveToHelper.hh>
#include <ignition/rendering/RenderingIface.hh>
#include <ignition/rendering/Scene.hh>
#include <ignition/transport/Node.hh>

#include "ignition/gazebo/Entity.hh"
#include "ignition/gazebo/gui/GuiEvents.hh"

namespace ignition::gazebo
{
  class ViewAnglePrivate
  {
    /// \brief Perform rendering calls in the rendering thread.
    public: void OnRender();

    /// \brief Callback when an animation is complete
    private: void OnComplete();

    /// \brief Ignition communication node.
    public: transport::Node node;

    /// \brief Mutex to protect angle mode
    public: std::mutex mutex;

    /// \brief View Angle service name
    public: std::string viewAngleService;

    /// \brief View Control service name
    public: std::string viewControlService;

    /// \brief View Control reference visual service name
    public: std::string viewControlRefVisualService;

    /// \brief View Control sensitivity service name
    public: std::string viewControlSensitivityService;

    /// \brief Move gui camera to pose service name
    public: std::string moveToPoseService;

    /// \brief Move gui camera to model service name
    public: std::string moveToModelService;

    /// \brief New move to model message
    public: bool newMoveToModel = false;

    /// \brief Distance of the camera to the model
    public: double distanceMoveToModel = 0.0;

    /// \brief Camera horizontal fov
    public: double horizontalFov = 0.0;

    /// \brief Flag indicating if there is a new camera horizontalFOV
    /// coming from qml side
    public: bool newHorizontalFOV = false;

    /// \brief gui camera pose
    public: math::Pose3d camPose;

    /// \brief GUI camera near/far clipping distance (QList order is near, far)
    public: QList<double> camClipDist{0.0, 0.0};

    /// \brief Flag indicating if there is a new camera clipping distance
    /// coming from qml side
    public: bool newCamClipDist = false;

    /// \brief Checks if there is new camera clipping distance from gui camera,
    /// used to update qml side
    /// \return True if there is a new clipping distance from gui camera
    public: bool UpdateQtCamClipDist();

    /// \brief View Control type
    public: rendering::CameraProjectionType viewControlType =
              rendering::CameraProjectionType::CPT_PERSPECTIVE;

    /// \brief Checks if there is a new view controller, used to update qml side
    /// \return True if there is a new view controller from gui camera
    public: bool UpdateQtViewControl();

    /// \brief Checks if there is new camera horizontal fov from gui camera,
    /// used to update qml side
    /// \return True if there is a new horizontal fov from gui camera
    public: bool UpdateQtCamHorizontalFOV();

    /// \brief User camera
    public: rendering::CameraPtr camera{nullptr};

    /// \brief Flag for indicating whether we are in view angle mode or not
    public: bool viewingAngle = false;

    /// \brief The pose set during a view angle button press that holds
    /// the pose the camera should assume relative to the entit(y/ies).
    /// The vector (0, 0, 0) indicates to return the camera back to the home
    /// pose originally loaded from the sdf.
    public: math::Vector3d viewAngleDirection = math::Vector3d::Zero;

    /// \brief Helper object to move user camera
    public: rendering::MoveToHelper moveToHelper;

    /// \brief The current selected entities
    public: std::vector<Entity> selectedEntities;

    /// \brief Last move to animation time
    public: std::chrono::time_point<std::chrono::system_clock> prevMoveToTime;

    /// \brief The pose set from the move to pose service.
    public: std::optional<math::Pose3d> moveToPoseValue;

    /// \brief Enable legacy features for plugin to work with GzScene3D.
    /// Disable them to work with the new MinimalScene plugin.
    public: bool legacy{false};
  };
}

using namespace ignition;
using namespace gazebo;

/////////////////////////////////////////////////
ViewAngle::ViewAngle()
  : ignition::gui::Plugin(), dataPtr(std::make_unique<ViewAnglePrivate>())
{
}

/////////////////////////////////////////////////
ViewAngle::~ViewAngle() = default;

/////////////////////////////////////////////////
void ViewAngle::LoadConfig(const tinyxml2::XMLElement *_pluginElem)
{
  if (this->title.empty())
    this->title = "View Angle";

  if (_pluginElem)
  {
    if (auto elem = _pluginElem->FirstChildElement("legacy"))
    {
      elem->QueryBoolText(&this->dataPtr->legacy);
    }
  }

  // For view angle requests
  this->dataPtr->viewAngleService = "/gui/view_angle";

  // view control requests
  this->dataPtr->viewControlService = "/gui/camera/view_control";

  // view control reference visual requests
  this->dataPtr->viewControlRefVisualService =
      "/gui/camera/view_control/reference_visual";

  // view control sensitivity requests
  this->dataPtr->viewControlSensitivityService =
      "/gui/camera/view_control/sensitivity";

  // Subscribe to camera pose
  std::string topic = "/gui/camera/pose";
  this->dataPtr->node.Subscribe(
    topic, &ViewAngle::CamPoseCb, this);

  // Move to pose service
  this->dataPtr->moveToPoseService = "/gui/move_to/pose";

  // Move to model service
  this->dataPtr->moveToModelService = "/gui/move_to/model";
  this->dataPtr->node.Advertise(this->dataPtr->moveToModelService,
      &ViewAngle::OnMoveToModelService, this);
  ignmsg << "Move to model service on ["
         << this->dataPtr->moveToModelService << "]" << std::endl;

  ignition::gui::App()->findChild<
    ignition::gui::MainWindow *>()->installEventFilter(this);
}

/////////////////////////////////////////////////
bool ViewAngle::eventFilter(QObject *_obj, QEvent *_event)
{
  if (_event->type() == ignition::gui::events::Render::kType)
  {
    this->dataPtr->OnRender();

    // updates qml cam clip distance spin boxes if changed
    if (this->dataPtr->UpdateQtCamClipDist())
    {
      this->CamClipDistChanged();
    }

    // updates qml cam horizontal fov spin boxes if changed
    if (this->dataPtr->UpdateQtCamHorizontalFOV())
    {
      this->CamHorizontalFOVChanged();
    }

    if (this->dataPtr->UpdateQtViewControl())
    {
      this->ViewControlIndexChanged();
    }
  }
  else if (_event->type() ==
           ignition::gazebo::gui::events::EntitiesSelected::kType)
  {
    auto selectedEvent =
        reinterpret_cast<gazebo::gui::events::EntitiesSelected *>(
        _event);

    if (selectedEvent && !selectedEvent->Data().empty())
    {
      for (const auto &_entity : selectedEvent->Data())
      {
        if (std::find(this->dataPtr->selectedEntities.begin(),
              this->dataPtr->selectedEntities.end(),
              _entity) != this->dataPtr->selectedEntities.end())
          continue;
        this->dataPtr->selectedEntities.push_back(_entity);
      }
    }
  }
  else if (_event->type() ==
           ignition::gazebo::gui::events::DeselectAllEntities::kType)
  {
    this->dataPtr->selectedEntities.clear();
  }

  // Standard event processing
  return QObject::eventFilter(_obj, _event);
}

/////////////////////////////////////////////////
void ViewAngle::OnAngleMode(int _x, int _y, int _z)
{
  // Legacy mode: request view angle from GzScene3d
  if (this->dataPtr->legacy)
  {
    std::function<void(const msgs::Boolean &, const bool)> cb =
        [](const msgs::Boolean &/*_rep*/, const bool _result)
    {
      if (!_result)
        ignerr << "Error setting view angle mode" << std::endl;
    };

    msgs::Vector3d req;
    req.set_x(_x);
    req.set_y(_y);
    req.set_z(_z);

    this->dataPtr->node.Request(this->dataPtr->viewAngleService, req, cb);
  }
  // New behaviour: handle camera movement here
  else
  {
    this->dataPtr->viewingAngle = true;
    this->dataPtr->viewAngleDirection = math::Vector3d(_x, _y, _z);
  }
}

/////////////////////////////////////////////////
void ViewAngle::OnViewControl(const QString &_controller)
{
  std::function<void(const msgs::Boolean &, const bool)> cb =
      [](const msgs::Boolean &/*_rep*/, const bool _result)
  {
    if (!_result)
      ignerr << "Error setting view controller" << std::endl;
  };

  msgs::StringMsg req;
  std::string str = _controller.toStdString();
  if (str.find("Orbit") != std::string::npos)
    req.set_data("orbit");
  else if (str.find("Ortho") != std::string::npos)
    req.set_data("ortho");
  else
  {
    ignerr << "Unknown view controller selected: " << str << std::endl;
    return;
  }

  this->dataPtr->node.Request(this->dataPtr->viewControlService, req, cb);
}

/////////////////////////////////////////////////
void ViewAngle::OnViewControlReferenceVisual(bool _enable)
{
  std::function<void(const msgs::Boolean &, const bool)> cb =
      [](const msgs::Boolean &/*_rep*/, const bool _result)
  {
    if (!_result)
      ignerr << "Error setting view controller reference visual" << std::endl;
  };

  msgs::Boolean req;
  req.set_data(_enable);

  this->dataPtr->node.Request(
      this->dataPtr->viewControlRefVisualService, req, cb);
}

/////////////////////////////////////////////////
void ViewAngle::OnViewControlSensitivity(double _sensitivity)
{
  std::function<void(const msgs::Boolean &, const bool)> cb =
      [](const msgs::Boolean &/*_rep*/, const bool _result)
  {
    if (!_result)
      ignerr << "Error setting view controller sensitivity" << std::endl;
  };

  if (_sensitivity <= 0.0)
  {
    ignerr << "View controller sensitivity must be greater than 0" << std::endl;
    return;
  }

  msgs::Double req;
  req.set_data(_sensitivity);

  this->dataPtr->node.Request(
      this->dataPtr->viewControlSensitivityService, req, cb);
}

/////////////////////////////////////////////////
QList<double> ViewAngle::CamPose() const
{
  return QList({
    this->dataPtr->camPose.Pos().X(),
    this->dataPtr->camPose.Pos().Y(),
    this->dataPtr->camPose.Pos().Z(),
    this->dataPtr->camPose.Rot().Roll(),
    this->dataPtr->camPose.Rot().Pitch(),
    this->dataPtr->camPose.Rot().Yaw()
  });
}

/////////////////////////////////////////////////
void ViewAngle::SetCamPose(double _x, double _y, double _z,
                           double _roll, double _pitch, double _yaw)
{
  this->dataPtr->camPose.Set(_x, _y, _z, _roll, _pitch, _yaw);

  // Legacy mode: request view angle from GzScene3d
  if (this->dataPtr->legacy)
  {
    std::function<void(const msgs::Boolean &, const bool)> cb =
        [](const msgs::Boolean &/*_rep*/, const bool _result)
    {
      if (!_result)
        ignerr << "Error sending move camera to pose request" << std::endl;
    };

    msgs::GUICamera req;
    msgs::Set(req.mutable_pose(), this->dataPtr->camPose);

    this->dataPtr->node.Request(this->dataPtr->moveToPoseService, req, cb);
  }
  // New behaviour: handle camera movement here
  else
  {
    this->dataPtr->moveToPoseValue = {_x, _y, _z, _roll, _pitch, _yaw};
  }
}

/////////////////////////////////////////////////
bool ViewAngle::OnMoveToModelService(const ignition::msgs::GUICamera &_msg,
  ignition::msgs::Boolean &_res)
{
  std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
  auto scene = this->dataPtr->camera->Scene();

  auto visualToMove = scene->VisualByName(_msg.name());
  if (nullptr == visualToMove)
  {
    ignerr << "Failed to get visual with ID ["
           << _msg.name() << "]" << std::endl;
    _res.set_data(false);
    return false;
  }
  Entity entityId = kNullEntity;
  try
  {
    // TODO(ahcorde): When forward porting this to Garder change var type to
    // unsigned int
    entityId = std::get<int>(visualToMove->UserData("gazebo-entity"));
  }
  catch(std::bad_variant_access &_e)
  {
    ignerr << "Failed to get gazebo-entity user data ["
           << visualToMove->Name() << "]" << std::endl;
    _res.set_data(false);
    return false;
  }

  math::Quaterniond q(
    _msg.pose().orientation().w(),
    _msg.pose().orientation().x(),
    _msg.pose().orientation().y(),
    _msg.pose().orientation().z());

  ignition::math::Vector3d axis;
  double angle;
  q.ToAxis(axis, angle);

  std::function<void(const msgs::Boolean &, const bool)> cb =
      [](const msgs::Boolean &/*_rep*/, const bool _result)
  {
    if (!_result)
      ignerr << "Error setting view controller" << std::endl;
  };

  msgs::StringMsg req;
  std::string str = _msg.projection_type();
  if (str.find("Orbit") != std::string::npos ||
      str.find("orbit") != std::string::npos)
  {
    req.set_data("orbit");
  }
  else if (str.find("Ortho") != std::string::npos ||
           str.find("ortho") != std::string::npos )
  {
    req.set_data("ortho");
  }
  else
  {
    ignerr << "Unknown view controller selected: " << str << std::endl;
    _res.set_data(false);
    return false;
  }

  this->dataPtr->node.Request(this->dataPtr->viewControlService, req, cb);

  this->dataPtr->viewingAngle = true;
  this->dataPtr->newMoveToModel = true;
  this->dataPtr->viewAngleDirection = axis;
  this->dataPtr->distanceMoveToModel = _msg.pose().position().z();
  this->dataPtr->selectedEntities.push_back(entityId);

  _res.set_data(true);
  return true;
}

/////////////////////////////////////////////////
void ViewAngle::CamPoseCb(const msgs::Pose &_msg)
{
  std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
  math::Pose3d pose = msgs::Convert(_msg);

  if (pose != this->dataPtr->camPose)
  {
    this->dataPtr->camPose = pose;
    this->CamPoseChanged();
  }
}

/////////////////////////////////////////////////
double ViewAngle::HorizontalFOV() const
{
  return this->dataPtr->horizontalFov;
}

/////////////////////////////////////////////////
void ViewAngle::SetHorizontalFOV(double _horizontalFOV)
{
  this->dataPtr->horizontalFov = _horizontalFOV;
  this->dataPtr->newHorizontalFOV = true;
}

/////////////////////////////////////////////////
QList<double> ViewAngle::CamClipDist() const
{
  return this->dataPtr->camClipDist;
}

/////////////////////////////////////////////////
void ViewAngle::SetCamClipDist(double _near, double _far)
{
  this->dataPtr->camClipDist[0] = _near;
  this->dataPtr->camClipDist[1] = _far;
  this->dataPtr->newCamClipDist = true;
}

/////////////////////////////////////////////////
void ViewAnglePrivate::OnRender()
{
  if (!this->camera)
  {
    auto scene = rendering::sceneFromFirstRenderEngine();
    if (!scene)
      return;

    for (unsigned int i = 0; i < scene->NodeCount(); ++i)
    {
      auto cam = std::dynamic_pointer_cast<rendering::Camera>(
        scene->NodeByIndex(i));
      if (cam)
      {
        bool isUserCamera = false;
        try
        {
          isUserCamera = std::get<bool>(cam->UserData("user-camera"));
        }
        catch (std::bad_variant_access &)
        {
          continue;
        }
        if (isUserCamera)
        {
          this->camera = cam;
          this->moveToHelper.SetInitCameraPose(this->camera->WorldPose());
          igndbg << "ViewAngle plugin is moving camera ["
                 << this->camera->Name() << "]" << std::endl;
          break;
        }
      }
    }

    if (!this->camera)
    {
      ignerr << "ViewAngle camera is not available" << std::endl;
      return;
    }
  }

  // View angle
  if (this->viewingAngle)
  {
    if (this->moveToHelper.Idle())
    {
      // Look at the origin if no entities are selected
      math::Vector3d lookAt = math::Vector3d::Zero;
      if (!this->selectedEntities.empty())
      {
        for (const auto &entity : this->selectedEntities)
        {
          for (auto i = 0u; i < this->camera->Scene()->VisualCount();
              ++i)
          {
            auto vis = this->camera->Scene()->VisualByIndex(i);
            if (!vis)
              continue;

            try
            {
              if (std::get<int>(vis->UserData("gazebo-entity")) !=
                  static_cast<int>(entity))
              {
                continue;
              }
            }
            catch (std::bad_variant_access &)
            {
              continue;
            }

            auto pos = vis->WorldPose().Pos();
            lookAt += pos;
          }
        }
        lookAt /= this->selectedEntities.size();
      }

      this->moveToHelper.LookDirection(this->camera,
          this->viewAngleDirection, lookAt,
          0.5, std::bind(&ViewAnglePrivate::OnComplete, this));
      this->prevMoveToTime = std::chrono::system_clock::now();
    }
    else
    {
      auto now = std::chrono::system_clock::now();
      std::chrono::duration<double> dt = now - this->prevMoveToTime;
      this->moveToHelper.AddTime(dt.count());
      this->prevMoveToTime = now;
    }
  }

  // Move to pose
  if (this->moveToPoseValue)
  {
    if (this->moveToHelper.Idle())
    {
      this->moveToHelper.MoveTo(this->camera,
          *(this->moveToPoseValue),
          0.5, std::bind(&ViewAnglePrivate::OnComplete, this));
      this->prevMoveToTime = std::chrono::system_clock::now();
    }
    else
    {
      auto now = std::chrono::system_clock::now();
      std::chrono::duration<double> dt = now - this->prevMoveToTime;
      this->moveToHelper.AddTime(dt.count());
      this->prevMoveToTime = now;
    }
  }

  // Camera clipping plane distance
  if (this->newCamClipDist)
  {
    this->camera->SetNearClipPlane(this->camClipDist[0]);
    this->camera->SetFarClipPlane(this->camClipDist[1]);
    this->newCamClipDist = false;
  }

  // Camera horizontalFOV
  if (this->newHorizontalFOV)
  {
    this->camera->SetHFOV(gz::math::Angle(this->horizontalFov));
    this->newHorizontalFOV = false;
  }
}

/////////////////////////////////////////////////
void ViewAnglePrivate::OnComplete()
{
  this->viewingAngle = false;
  this->moveToPoseValue.reset();
  if (this->newMoveToModel)
  {
    this->selectedEntities.pop_back();
    this->newMoveToModel = false;

    auto cameraPose = this->camera->WorldPose();
    auto distance = -(this->viewAngleDirection * this->distanceMoveToModel);

    if (!math::equal(this->viewAngleDirection.X(), 0.0))
    {
      cameraPose.Pos().X(distance.X());
    }
    if (!math::equal(this->viewAngleDirection.Y(), 0.0))
    {
      cameraPose.Pos().Y(distance.Y());
    }
    if (!math::equal(this->viewAngleDirection.Z(), 0.0))
    {
      cameraPose.Pos().Z(distance.Z());
    }

    this->moveToPoseValue = {
      cameraPose.Pos().X(),
      cameraPose.Pos().Y(),
      cameraPose.Pos().Z(),
      cameraPose.Rot().Roll(),
      cameraPose.Rot().Pitch(),
      cameraPose.Rot().Yaw()};
  }
}

/////////////////////////////////////////////////
bool ViewAnglePrivate::UpdateQtCamHorizontalFOV()
{
  bool updated = false;
  if (std::abs(this->camera->HFOV().Radian() - this->horizontalFov) > 0.0001)
  {
    this->horizontalFov = this->camera->HFOV().Radian();
    updated = true;
  }
  return updated;
}

/////////////////////////////////////////////////
bool ViewAnglePrivate::UpdateQtCamClipDist()
{
  bool updated = false;
  if (std::abs(this->camera->NearClipPlane() - this->camClipDist[0]) > 0.0001)
  {
    this->camClipDist[0] = this->camera->NearClipPlane();
    updated = true;
  }

  if (std::abs(this->camera->FarClipPlane() - this->camClipDist[1]) > 0.0001)
  {
    this->camClipDist[1] = this->camera->FarClipPlane();
    updated = true;
  }
  return updated;
}

/////////////////////////////////////////////////
int ViewAngle::ViewControlIndex() const
{
  if (this->dataPtr->viewControlType ==
        rendering::CameraProjectionType::CPT_PERSPECTIVE)
    return 0;

  return 1;
}

/////////////////////////////////////////////////
bool ViewAnglePrivate::UpdateQtViewControl()
{
  if (!this->camera)
    return false;

  if (this->camera->ProjectionType() != this->viewControlType)
  {
    this->viewControlType = this->camera->ProjectionType();
    return true;
  }

  return false;
}

// Register this plugin
IGNITION_ADD_PLUGIN(ViewAngle,
                    ignition::gui::Plugin)

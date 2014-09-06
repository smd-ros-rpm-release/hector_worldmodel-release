#include "ObjectModel.h"
#include "Object.h"

namespace hector_object_tracker {

ObjectModel::ObjectModel(const std::string& frame_id)
{
  setFrameId(frame_id);
}

ObjectModel::ObjectModel(const ObjectModel &other)
{
  *this = other;
}

ObjectModel::~ObjectModel()
{}

ObjectList ObjectModel::getObjects() const
{
  boost::recursive_mutex::scoped_lock lock(objectsMutex);
  return objects;
}

ObjectList ObjectModel::getObjects(const std::string& class_id) const
{
  boost::recursive_mutex::scoped_lock lock(objectsMutex);
  ObjectList class_list;

  for(const_iterator it = begin(); it != end(); ++it) {
    if ((*it)->getClassId() == class_id) class_list.push_back(*it);
  }

  return class_list;
}

ObjectPtr ObjectModel::getObject(const std::string& object_id) const {
  boost::recursive_mutex::scoped_lock lock(objectsMutex);

  for(const_iterator it = begin(); it != end(); ++it) {
    if ((*it)->getObjectId() == object_id) return *it;
  }

  return ObjectPtr();
}

std_msgs::Header ObjectModel::getHeader() const {
  std_msgs::Header temp;
  temp.frame_id = header.frame_id;

  for(const_iterator it = begin(); it != end(); ++it) {
    if ((*it)->getStamp() > temp.stamp) temp.stamp = (*it)->getStamp();
  }
  return temp;
}

void ObjectModel::setFrameId(const std::string &frame_id) {
  header.frame_id = frame_id;
}

void ObjectModel::getMessage(hector_worldmodel_msgs::ObjectModel& model) const {
  boost::recursive_mutex::scoped_lock lock(objectsMutex);

  model.header = getHeader();
  model.objects.clear();
  model.objects.reserve(objects.size());
  for(ObjectList::const_iterator it = objects.begin(); it != objects.end(); ++it) {
    model.objects.push_back((*it)->getMessage());
  }
}

hector_worldmodel_msgs::ObjectModelPtr ObjectModel::getMessage() const {
  hector_worldmodel_msgs::ObjectModelPtr model(new hector_worldmodel_msgs::ObjectModel());
  getMessage(*model);
  return model;
}

void ObjectModel::reset()
{
  boost::recursive_mutex::scoped_lock lock(objectsMutex);
  objects.clear();
  Object::reset();
}

ObjectPtr ObjectModel::add(const std::string& class_id, const std::string& object_id) {
  return add(ObjectPtr(new Object(class_id, object_id)));
}

ObjectPtr ObjectModel::add(ObjectPtr object) {
  objects.push_back(object);
  return object;
}

void ObjectModel::remove(ObjectPtr object) {
  for(ObjectList::iterator it = objects.begin(); it != objects.end(); ++it) {
    if (*it == object) {
      remove(it);
      return;
    }
  }
}

void ObjectModel::remove(iterator it) {
  objects.erase(it);
}
ObjectModel& ObjectModel::operator =(const ObjectModel& other)
{
  header = other.header;
  objects = other.objects;
  return *this;
}

ObjectModel& ObjectModel::operator =(const hector_worldmodel_msgs::ObjectModel& other)
{
  header = other.header;

  for(hector_worldmodel_msgs::ObjectModel::_objects_type::const_iterator it = other.objects.begin();
      it != other.objects.end();
      ++it)
  {
    ObjectPtr object = getObject(it->info.object_id);
    if (!object) {
      object.reset(new Object(*it));
      add(object);
    } else {
      *object = *it;
    }
  }

  return *this;
}

void ObjectModel::getVisualization(visualization_msgs::MarkerArray &markers) const {
  boost::recursive_mutex::scoped_lock lock(objectsMutex);

  markers.markers.clear();
  for(ObjectList::const_iterator it = objects.begin(); it != objects.end(); ++it) {
    (*it)->getVisualization(markers);
  }
}

float ObjectModel::getBestCorrespondence(ObjectPtr &object, const Eigen::Vector3f& position, const Eigen::Matrix3f& covariance, const std::string& class_id, float max_distance) const
{
  float min_distance = max_distance;
  if (min_distance <= 0.0) min_distance = FLT_MAX;

  object.reset();

  for(ObjectModel::const_iterator it = begin(); it != end(); ++it) {
    ObjectPtr x = *it;
    if (!class_id.empty() && class_id != x->getClassId()) continue;
    Eigen::Vector3f diff = x->getPosition() - position;
    float distance = (diff.transpose() * (x->getCovariance() + covariance).inverse() * diff)[0];
    if (distance < min_distance) {
      object = x;
      min_distance = distance;
    }
  }

  return min_distance;
}

void ObjectModel::mergeWith(const ObjectModel &other_model, tf::TransformListener &tf, const std::string& prefix) {
  for(ObjectModel::const_iterator other = other_model.begin(); other != other_model.end(); ++other) {
    merge(*other, tf, prefix);
  }
}

void ObjectModel::merge(const ObjectPtr& object, tf::TransformListener &tf, const std::string& prefix)
{
  // transform other object's pose
  ObjectPtr transformed;
  try {
    transformed = object->transform(tf, header.frame_id, ros::Time());
  } catch (tf::TransformException& ex) {
    ROS_WARN("Could not transform from frame %s into %s during object merge: %s", object->getHeader().frame_id.c_str(), header.frame_id.c_str(), ex.what());
    return;
  }

  // search for corresponding objects
  ObjectPtr mine;
  float distance = getBestCorrespondence(mine, transformed->getPosition(), transformed->getCovariance(), object->getClassId());
  if (distance < 1.0) {
    // found corresondence
    ROS_DEBUG("Merging %s and %s", mine->getObjectId().c_str(), object->getObjectId().c_str());
    mine->setObjectId(mine->getObjectId() + "," + prefix + object->getObjectId());
    mine->update(transformed->getPosition(), transformed->getCovariance(), object->getSupport());

  } else {
    // add as new object
    ROS_DEBUG("Adding %s", transformed->getObjectId().c_str());
    transformed->setObjectId(prefix + object->getObjectId());
    add(transformed);
  }
}

} // namespace hector_object_tracker

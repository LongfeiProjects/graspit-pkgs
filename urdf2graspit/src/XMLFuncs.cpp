/**
    Copyright (C) 2015 Jennifer Buehler

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software Foundation,
    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**/

#include <urdf2graspit/XMLFuncs.h>
#include <string>
#include <vector>

using urdf2graspit::xmlfuncs::FingerChain;
using urdf2graspit::JointPtr;
using urdf2graspit::JointConstPtr;
using urdf2graspit::InertialPtr;

using urdf2graspit::DHParam;

#define RAD_TO_DEG 180/M_PI

FingerChain& FingerChain::operator=(const FingerChain& o)
{
    if (&o == this) return *this;
    prms = o.prms;
    linkFileNames = o.linkFileNames;
    linkTypes = o.linkTypes;
    return *this;
}

std::ostream& operator<<(std::ostream& o, const FingerChain& p)
{
    o << "Joints: " << std::endl;
    for (std::vector<DHParam>::const_iterator it = p.prms.begin(); it != p.prms.end(); ++it)
    {
        o << *it << std::endl;
    }
    o << "Links:" << std::endl;
    std::vector<std::string>::const_iterator t = p.linkTypes.begin();
    for (std::vector<std::string>::const_iterator it = p.linkFileNames.begin(); it != p.linkFileNames.end(); ++it)
    {
        o << *it << ", type=" << *t << std::endl;
        ++t;
    }
    return o;
}


// \param scale scale min/max values to degrees for revolute joints, and to mm for prismatic joints 
void getJointLimits(const urdf::Joint& j, float& min, float& max, bool negateJointValues, bool scale=true)
{
    min = j.limits->lower;
    max = j.limits->upper;
    if (negateJointValues)
    {
        min = -min;
        max = -max;
    }
    if (!scale) return;
    bool revolute = j.type == urdf::Joint::REVOLUTE;
    if (revolute)
    {
        min *= RAD_TO_DEG;
        max *= RAD_TO_DEG;
    }
    else
    {   // convert from meter units to mm
        min *= 1000;
        max *= 1000;
    }
}


std::string getHeader(const std::string& robottype, const std::string& palm_xml_file)
{
    std::stringstream str;
    str << "<?xml version='1.0'?>" << std::endl;
    str << "<robot type='" << robottype << "'>" << std::endl;
    str << "<palm>" << palm_xml_file << "</palm>";
    return str.str();
}

std::string getEigenGraspValues(const std::vector<DHParam>& dhparams, bool negateJointValues)
{
    float minAmpl =
    float maxAmpl = 
    std::stringstream str;
    str << "<EG>" << std::endl;
    str << "<EigenValue value=\"0.5\"/> <!--EigenValue is not used in the code at this stage -->" << std::endl;
    str<<"<Limits min=\""<<minAmpl<<"\" max=\""<<maxAmpl<<"\"/>"<<std::endl;
    str << "<DimVals ";
    int i = 0;
    for (std::vector<DHParam>::const_iterator it = dhparams.begin(); it != dhparams.end(); ++it, ++i)
    {
        float minValue, maxValue;
        getJointLimits(*(it->joint), minValue, maxValue, negateJointValues, false);
        str << " d" << i << "=\"" << num << "\"";
    }
    str << "/>" << std::endl;
    str << "</EG>" << std::endl;
    return str.str();
}

std::string urdf2graspit::xmlfuncs::getEigenGraspXML(const std::vector<DHParam>& dhparams, bool negateJointValues)
{
    std::stringstream str;
    str << "<?xml version=\"1.0\" ?>" << std::endl;

   // print all joint names so it's easier to edit the EigenGrasp file.
    unsigned int i=0;
    for (std::vector<DHParam>::const_iterator it = dhparams.begin(); it != dhparams.end(); ++it, ++i)
    {
        float minValue, maxValue;
        getJointLimits(*(it->joint), minValue, maxValue, negateJointValues, false);
        str << "<!-- d" << i <<": "<<it->joint->name<<", min="<<minValue<<", max="<<maxValue<<" -->" << std::endl;
    }

    str << "<EigenGrasps dimensions=\"" << dhparams.size() << "\">" << std::endl;

    str << getEigenGraspValues(dhparams, negateJointValues);

    str << "<ORIGIN>" << std::endl;
    str << "<EigenValue value=\"0.5\"/> " << std::endl;
    str << "<DimVals";

    int i = 0;
    for (std::vector<DHParam>::const_iterator it = dhparams.begin(); it != dhparams.end(); ++it)
    {
        float minValue, maxValue;
        getJointLimits(*(it->joint), minValue, maxValue, negateJointValues, true);
        float num = (maxValue - minValue) / 2;
        str << " d" << i << "=\"" << num << "\"";
        ++i;
    }
    str << "/>" << std::endl;

    str << "<!-- if empty, defaults to all zeros-->" << std::endl;
    str << "</ORIGIN>" << std::endl;
    str << "</EigenGrasps>" << std::endl;
    return str.str();
}


bool isRevolutingJoint(const JointConstPtr& joint)
{
    return (joint->type == urdf::Joint::REVOLUTE) || (joint->type == urdf::Joint::CONTINUOUS);
}
bool isPrismaticJoint(const JointConstPtr& joint)
{
    return (joint->type == urdf::Joint::PRISMATIC);
}



std::string getChainJointSpec(const DHParam& dh, bool negateJointValues)
{
    if (!isRevolutingJoint(dh.joint) && !isPrismaticJoint(dh.joint))
    {
        ROS_ERROR("Joint has to be revoluting or prismatic!");
        return std::string();
    }

    bool revolute = dh.joint->type == urdf::Joint::REVOLUTE;
    float minValue, maxValue;
    getJointLimits(*(dh.joint), minValue, maxValue, negateJointValues, true);
    std::stringstream ret;
    ret << "<joint type=" << (revolute ? "'Revolute'" : "'Prismatic'") << ">" << std::endl;

    // for some reason, referenced DOF for revolute joints has to be in <theta>, not <d> as
    // specified in documentation (http://www.cs.columbia.edu/~cmatei/graspit/html-manual/graspit-manual_4.html)
    // Instead, prismatic joints are specified in <d>
    if (isRevolutingJoint(dh.joint))  
        ret << "<theta> d" << dh.dof_index << "+"
            << dh.theta*RAD_TO_DEG << "</theta>" << std::endl;
    else ret << "<theta>" << dh.theta*RAD_TO_DEG << "</theta>" << std::endl;

    if (isPrismaticJoint(dh.joint))
         ret << "<d> d" <<dh.dof_index <<"+" << dh.d << "</d>" << std::endl;
    else ret << "<d>" << dh.d << "</d>" << std::endl;
    
    ret << "<a>" << dh.r << "</a>" << std::endl;
    ret << "<alpha>" << dh.alpha*RAD_TO_DEG << "</alpha>" << std::endl;
    
    ret << "<minValue>" << minValue << "</minValue>" << std::endl;
    ret << "<maxValue>" << maxValue << "</maxValue>" << std::endl;
    ret << "<viscousFriction>5.0e+7</viscousFriction>" << std::endl;
    ret << "</joint>" << std::endl;
    return ret.str();
}



std::string urdf2graspit::xmlfuncs::getFingerChain(const FingerChain& c, const Eigen::Vector3d& palmTranslation,
        const Eigen::Quaterniond& palmRotation, bool negateJointValues)
{
    std::stringstream str;
    // XXX for some reason, graspit wants the inverse of the rotation
    Eigen::Quaterniond corrPalmRotation = palmRotation.inverse();
    Eigen::Quaterniond::Matrix3 m = corrPalmRotation.toRotationMatrix();
    str << "<chain> " << std::endl;
    str << "<transform> " << std::endl;
    str << "<translation>" << palmTranslation.x() << " "
        << palmTranslation.y() << " " << palmTranslation.z() << "</translation>" << std::endl;
    str << "<rotationMatrix>"
        << m(0, 0) << " " << m(0, 1) << " " << m(0, 2) << " "
        << m(1, 0) << " " << m(1, 1) << " " << m(1, 2) << " "
        << m(2, 0) << " " << m(2, 1) << " " << m(2, 2) << " "
        << "</rotationMatrix> " << std::endl;
    str << "</transform>" << std::endl;
    for (std::vector<DHParam>::const_iterator it = c.prms.begin(); it != c.prms.end(); ++it)
    {
        std::string j = getChainJointSpec(*it, negateJointValues);
        str << j;
    }

    std::vector<std::string>::const_iterator t = c.linkTypes.begin();
    for (std::vector<std::string>::const_iterator it = c.linkFileNames.begin(); it != c.linkFileNames.end(); ++it)
    {
        str << "<link dynamicJointType='" << *t << "'>" << *it << "</link>" << std::endl;
        ++t;
    }
    str << "</chain>" << std::endl;
    return str.str();
}


std::string urdf2graspit::xmlfuncs::getDOF(float defaultVel, float maxEffort, float kp,
        float kd, float draggerScale, const std::string& type)
{
    std::stringstream ret;
    ret << "<dof type='" << type << "'>" << std::endl;
    ret << "<defaultVelocity>" << defaultVel << "</defaultVelocity>" << std::endl;
    ret << "<maxEffort>" << maxEffort << "</maxEffort>" << std::endl;
    ret << "<Kp>" << kp << "</Kp>" << std::endl;
    ret << "<Kd>" << kd << "</Kd>" << std::endl;
    ret << "<draggerScale>" << draggerScale << "</draggerScale>" << std::endl;
    // ret<<"<breakAwayTorque>0.5</breakAwayTorque>"<<std::endl;
    ret << "</dof>" << std::endl;
    return ret.str();
}


std::string urdf2graspit::xmlfuncs::getLinkDescXML(
    const LinkPtr& link,
    const std::string& linkMeshFile,
    const std::string& material)
{
    InertialPtr i = link->inertial;

    std::stringstream str;
    str << "<?xml version=\"1.0\" ?>" << std::endl;
    str << "<root>" << std::endl;
    str << "<material>" << material << "</material>" << std::endl;
    float msc = 1000;
    str << "<mass>" << i->mass*msc << "</mass>" << std::endl;  // mass in grams
    str << "<cog>" << i->origin.position.x << " " << i->origin.position.y
        << " " << i->origin.position.z << "</cog>" << std::endl;
    str << "<inertia_matrix>"
        << i->ixx*msc << " " << i->ixy*msc << " " << i->ixz*msc << " "
        << i->ixy*msc << " " << " " << i->iyy*msc << " " << i->iyz*msc << " "
        << i->ixz*msc << " " << " " << i->iyz*msc << " " << i->izz*msc << "</inertia_matrix>" << std::endl;
    str << "<geometryFile>" << linkMeshFile << "</geometryFile>" << std::endl;
    str << "</root>" << std::endl;
    return str.str();
}




std::string urdf2graspit::xmlfuncs::getWorldFileTemplate(
    const std::string& robotName,
    const std::vector<DHParam>& dhparams,
    const std::string& robotFileRelToGraspitRoot,
    bool negateJointValues)
{
    bool includeCube = false;

    std::stringstream str;
    str << "<?xml version=\"1.0\"?>" << std::endl;
    str << "<world>" << std::endl;

    if (!includeCube) str << "<!--" << std::endl;
    str << "<graspableBody>" << std::endl;
    str << "<filename>models/objects/small_cube.xml</filename>" << std::endl;
    str << "<transform>" << std::endl;
    str << "<fullTransform>(+1 0 0 0)[+100 +0 +0]</fullTransform>" << std::endl;
    str << "</transform>" << std::endl;
    str << "</graspableBody>" << std::endl;
    if (!includeCube) str << "-->" << std::endl;

    str << "<robot>" << std::endl;
    str << "<filename>" << robotFileRelToGraspitRoot << "</filename>" << std::endl;


    str << "<dofValues>";
    for (std::vector<DHParam>::const_iterator it = dhparams.begin(); it != dhparams.end(); ++it)
    {
        float min, max;
        getJointLimits(*(it->joint), min, max, negateJointValues, false);
        // str<<(0.5*max + 0.5*min)<<" ";
         str << min << " ";
    }
    str << "</dofValues>" << std::endl;

    str << "<transform>" << std::endl;
    str << "<fullTransform>(+1 0 0 0)[0 0 0]</fullTransform>" << std::endl;
    str << "</transform>" << std::endl;
    str << "</robot>" << std::endl;
    str << "<camera>" << std::endl;
    str << "<position>+0 +0 +500</position>" << std::endl;
    str << "<orientation>0 0 0 1</orientation>" << std::endl;
    str << "<focalDistance>+500</focalDistance>" << std::endl;
    str << "</camera>" << std::endl;

    str << "</world>" << std::endl;
    str << "" << std::endl;

    return str.str();
}



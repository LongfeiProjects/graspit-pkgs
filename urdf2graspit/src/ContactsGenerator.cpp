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

#include <urdf2graspit/ContactsGenerator.h>
#include <urdf2inventor/Helpers.h>
//#include <urdf2graspit/XMLFuncs.h>
#include <urdf2graspit/MarkerSelector.h>
#include <urdf2graspit/ContactFunctions.h>

#include <string>
#include <ros/ros.h>
#include <ros/package.h>

#include <map>
#include <vector>
#include <set>

#define RAD_TO_DEG 180/M_PI

using urdf2graspit::ContactsGenerator;
using urdf2graspit::markerselector::MarkerSelector;


bool ContactsGenerator::transformToDHReferenceFrames(const std::vector<DHParam>& dh)
{
    std::map<std::string,EigenTransform> transforms;
    if (!DHParam::dh2urdfTransforms(dh, transforms))
    {
        ROS_ERROR("Could not get transforms from DH to URDF");
        return false; 
    }

    std::map<std::string,EigenTransform>::iterator it;
    for (it=transforms.begin(); it!=transforms.end(); ++it)
    {
            
        LinkPtr link=getLink(it->first);
        if (!link.get())
        {
            ROS_ERROR("Link %s does not exist", it->first.c_str());
            return false;
        }
        bool preApply = true;
        applyTransformToContacts(link, it->second, preApply);
    }
    
    return true;
}


void ContactsGenerator::applyTransformToContacts(LinkPtr& link, const EigenTransform& trans, bool preMult)
{
    std::map<std::string, std::vector<ContactPtr> >::iterator lCnt = linkContacts.find(link->name);
    if (lCnt != linkContacts.end())
    {
        for (std::vector<ContactPtr>::iterator it = lCnt->second.begin(); it != lCnt->second.end(); ++it)
        {
            ContactPtr c = *it;
            EigenTransform t = EigenTransform::Identity();
            t.translate(c->loc);
            t.rotate(c->ori);
            // ROS_INFO_STREAM("////// Applying transform "<<trans<<" to "<<c->loc);
            if (preMult) t = trans * t;
            else         t = t * trans;
            c->loc = t.translation();
            c->ori = Eigen::Quaterniond(t.rotation());
            c->norm = trans.rotation() * c->norm;
        }
    }
}


void ContactsGenerator::scaleContacts(double scale_factor)
{
    if (isContactsScaled) return;
    std::map<std::string, std::vector<ContactPtr> >::iterator linkItr;
    for (linkItr=linkContacts.begin(); linkItr!=linkContacts.end(); ++linkItr)
    {
        for (std::vector<ContactPtr>::iterator it = linkItr->second.begin(); it != linkItr->second.end(); ++it)
        {
            ContactPtr c = *it;
            c->loc *= scale_factor;
        }
    }
    isContactsScaled = true;
}

bool ContactsGenerator::generateContactsForallVisuals(const std::string& linkName, const int linkNum, const int fingerNum,
        const float coefficient, const std::vector<MarkerSelector::Marker>& markers)
{
    LinkPtr link = getLink(linkName);

    std::map<std::string, std::vector<ContactPtr> >::iterator lCnt = linkContacts.find(linkName);
    if (lCnt == linkContacts.end())
        lCnt = linkContacts.insert(std::make_pair(linkName, std::vector<ContactPtr>())).first;

    std::vector<MarkerSelector::Marker>::const_iterator m = markers.begin();
    int visualNum = -1;
    for (std::vector<VisualPtr >::iterator vit = link->visual_array.begin();
            vit != link->visual_array.end(); ++vit)
    {
        ++visualNum;

        if ((m != markers.end()) && (m->visualNum > visualNum)) continue;

        VisualPtr visual = *vit;

        EigenTransform vTrans = getTransform(visual->origin);   // transform from link reference frame to the visual

        // move forward in markers vector until we get the correct visual number
        while (m != markers.end())
        {
            // ROS_INFO("linkname %s vn %i",m->linkName.c_str(),m->visualNum);
            if (m->visualNum == visualNum)
            {
                break;
            }
            ++m;
        }

        // ROS_INFO("Checking Marker for link %s, visual num %i (m visual num %i)",
        //      linkName.c_str(),visualNum, m->visualNum);
        // if (m==markers.end()) ROS_INFO("No marker in map.");

        // loop over all marker entries we have for this visual number
        while ((m != markers.end()) && (m->visualNum == visualNum))
        {
            ContactPtr contact(new Contact());
            contact->linkNum = linkNum;
            contact->fingerNum = fingerNum;

            setUpSoftFrictionEdges(contact->numFrictionEdges, contact->frictionEdges);

            // ROS_INFO("Num friction edges: %i",contact.numFrictionEdges);

            // have to transform from the visual reference frame back to the link reference frame
            Eigen::Vector3d pos(m->coords);
            Eigen::Vector3d norm(m->normal);
            norm.normalize();

            // ROS_INFO_STREAM("Normal: "<<norm);

            // ROS_INFO_STREAM("Visual trans: "<<vTrans);

            EigenTransform toPos = EigenTransform::Identity();
            Eigen::Vector3d dNorm(0, 0, 1);
            // dNorm=vTrans.rotation()*dNorm;
            Eigen::Quaterniond rotNorm = Eigen::Quaterniond::FromTwoVectors(dNorm, norm);
            toPos.translate(pos);
            toPos.rotate(rotNorm);

            EigenTransform jtToPos = vTrans * toPos;
            pos = jtToPos.translation();
            contact->loc = pos;

            Eigen::Quaterniond ori(jtToPos.rotation());
            contact->ori = ori;

            // Contact normal
            norm = dNorm;
            norm.normalize();
            contact->norm = norm;
            contact->cof = coefficient;

            contacts.push_back(contact);
            lCnt->second.push_back(contact);

            // ROS_INFO_STREAM("Adding contact for link "<<linkName<<", visual "<<visualNum<<
            //    ": "<<contact<<" (mark: "<<*m<<") vis trans: "<<vTrans.translation());

            ++m;
        }
    }

    return true;
}


bool ContactsGenerator::generateContacts(const std::vector<std::string>& rootFingerJoints, const std::string& palmLinkName,
        const float coefficient, const MarkerSelector::MarkerMap& markers,
        const std::vector<DHParam>& dh)
{
    LinkPtr palm_link;
    getRobot().getLink(palmLinkName, palm_link);

    // first, do the palm:
    MarkerSelector::MarkerMap::const_iterator palmM = markers.find(palmLinkName);
    if (palmM != markers.end())
    {
        int linkNum = 0;
        int fingerNum = -1;
        if (!generateContactsForallVisuals(palmLinkName, linkNum, fingerNum, coefficient, palmM->second))
        {
            ROS_ERROR("Failed to generate contact for link %s", palmLinkName.c_str());
            return false;
        }
    }

    // now, do all the fingers:
    int fingerNum = -1;
    for (std::vector<std::string>::const_iterator it = rootFingerJoints.begin(); it != rootFingerJoints.end(); ++it)
    {
        ++fingerNum;
        // ROS_INFO("Handling root finger %s",it->c_str());

        JointPtr root_joint = getJoint(*it);
        if (!root_joint.get())
        {
            ROS_ERROR("Could not find joint %s", it->c_str());
            return false;
        }
        std::vector<JointPtr> chain;
        bool onlyActive = true;
        if (!getDependencyOrderedJoints(chain, root_joint, false, onlyActive) || chain.empty())
        {
            ROS_ERROR("Could not get joint chain, joint %s", root_joint->name.c_str());
            return false;
        }

        int linkNum = -1;
        for (std::vector<JointPtr>::iterator cit = chain.begin(); cit != chain.end(); ++cit)
        {
            ++linkNum;
            JointPtr chainJoint = *cit;
            std::string linkName = chainJoint->child_link_name;

            MarkerSelector::MarkerMap::const_iterator markerIt = markers.find(linkName);
            if (markerIt == markers.end())
            {
                // no markers defined for this link
                // ROS_INFO("No markers defined for this link: %s",linkName.c_str());
                continue;
            }


            // ROS_INFO("Marker defined for link %s",linkName.c_str());

            if (!generateContactsForallVisuals(linkName, linkNum, fingerNum, coefficient, markerIt->second))
            {
                ROS_ERROR("Failed to generate contact for link %s", linkName.c_str());
                return false;
            }
        }
    }

    if (!transformToDHReferenceFrames(dh))
    {
        ROS_ERROR("Could not transform the contacts to DH parameter space");
        return false;
    }
    scaleContacts(getScaleFactor());
    return true;
}


std::string ContactsGenerator::getContactsFileContent(const std::string& robotName)
{
    std::stringstream str;

    // Robot name
    str << robotName << std::endl;

    // Number of contacts
    int contSize = contacts.size();
    str << contSize << std::endl;

    //ROS_INFO_STREAM("Number of contacts: "<<contSize);

    for (std::vector<ContactPtr>::const_iterator cit = contacts.begin(); cit != contacts.end(); ++cit)
    {
        ContactPtr c = *cit;

        //ROS_INFO_STREAM("Contact: "<<*c);

        str << c->fingerNum << " " << c->linkNum << std::endl;
        str << c->numFrictionEdges << std::endl;

        // The friction edges. One line for each friction edge (total number defined in 2) ).
        //  6 * <number-friction-edges> values.
        for (unsigned int i = 0; i < c->numFrictionEdges; ++i)
        {
            for (unsigned int j = 0; j < 6; ++j)
            {
                str << c->frictionEdges[i * 6 + j] << " ";
            }
            str << std::endl;
        }

        str << static_cast<float>(c->loc.x()) << " "
            << static_cast<float>(c->loc.y()) << " "
            << static_cast<float>(c->loc.z()) << std::endl;

        str << static_cast<float>(c->ori.w()) << " "
            << static_cast<float>(c->ori.x()) << " "
            << static_cast<float>(c->ori.y()) << " "
            << static_cast<float>(c->ori.z()) << std::endl;

        // Frame location (mostly()) equals virtual contact location)
        str << static_cast<float>(c->loc.x()) << " "
            << static_cast<float>(c->loc.y()) << " "
            << static_cast<float>(c->loc.z()) << std::endl;

        str << static_cast<float>(c->norm.x()) << " "
            << static_cast<float>(c->norm.y()) << " "
            << static_cast<float>(c->norm.z()) << std::endl;

        str << static_cast<float>(c->cof) << std::endl;
    }

    return str.str();
}


bool ContactsGenerator::generateContactsWithViewer(const std::vector<std::string>& fingerRoots,
        const std::string& palmLinkName, float standard_coefficient,
        const std::vector<DHParam>& dh)
{
    if (!prepareModelForDenavitHartenberg(palmLinkName))
    {
        ROS_ERROR("Could not prepare for DH parameter compatible URDF model.");
        return false;
    }

    bool success = true;
    MarkerSelector markerSelector(0.002);
    markerSelector.init("Marker selector");
    SoNode * node = getAsInventor(palmLinkName,false);
    if (!node)
    {
        ROS_ERROR("Could not get inventor node");
        return false;
    }

    ROS_INFO_STREAM("Model inventor files loaded, now loading into viewer...");
    // XXX TEST for some mesh formats, it does not work if init() is called above,
    // it has to be called here instead. The problem is the call of SoQt::init().
    // If called at all, also subsequent mesh conversions fail. I think it's a problem
    // with ivcon. It was so far found with the .obj meshes of the R2, not with the jaco
    // STL meshes...
    //markerSelector.init("Marker selector");
    markerSelector.loadModel(node);
    markerSelector.runViewer();

    MarkerSelector::MarkerMap markers = markerSelector.getMarkers();

    // ROS_INFO("Number of contacts: %lu",markers.size());
    // ROS_INFO("Markers: %s",markerSelector.toString().c_str());

    if (!generateContacts(fingerRoots, palmLinkName, standard_coefficient, markers, dh))
    {
        ROS_ERROR("could not generate contacts");
        return false;
    }
    return true;
}


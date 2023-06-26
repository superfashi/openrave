// -*- coding: utf-8 -*-
// Copyright (C) 2013 Rosen Diankov <rosen.diankov@gmail.com>
//
// This file is part of OpenRAVE.
// OpenRAVE is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
#include "jsoncommon.h"
#include "stringutils.h"

#include <boost/interprocess/streams/vectorstream.hpp>
#include <openrave/openravejson.h>
#include <openrave/openravemsgpack.h>
#include <fstream>
#include <rapidjson/writer.h>
#include <rapidjson/ostreamwrapper.h>

namespace OpenRAVE {

class EnvironmentJSONWriter
{
public:
    EnvironmentJSONWriter(const AttributesList& atts, rapidjson::Value& rEnvironment, rapidjson::Document::AllocatorType& allocator) : _rEnvironment(rEnvironment), _allocator(allocator) {
        _serializeOptions = 0;
        FOREACHC(itatt,atts) {
            if( itatt->first == "openravescheme" ) {
                _vForceResolveOpenRAVEScheme = itatt->second;
            }
            else if( itatt->first == "uriHint" ) {
                if( itatt->second == "1" ) {
                    _serializeOptions = ISO_ReferenceUriHint;
                }
            }
        }
    }

    virtual ~EnvironmentJSONWriter() {
    }

    virtual void Write(EnvironmentBasePtr penv) {
        dReal fUnitScale = 1.0;
        EnvironmentBase::EnvironmentBaseInfo info;
        penv->ExtractInfo(info);
        _rEnvironment.SetObject();
        info.SerializeJSON(_rEnvironment, _allocator, fUnitScale, _serializeOptions);
    }

    virtual void Write(KinBodyPtr pbody) {
        std::list<KinBodyPtr> listbodies;
        listbodies.push_back(pbody);
        _Write(listbodies);
    }

    virtual void Write(const std::list<KinBodyPtr>& listbodies) {
        _Write(listbodies);
    }

protected:

    virtual void _Write(const std::list<KinBodyPtr>& listbodies) {
        _rEnvironment.SetObject();
        if (listbodies.size() > 0) {
            EnvironmentBaseConstPtr penv = listbodies.front()->GetEnv();
            OpenRAVE::orjson::SetJsonValueByKey(_rEnvironment, "unit", penv->GetUnit(), _allocator);
            dReal fUnitScale = 1.0;

            FOREACHC(itbody, listbodies) {
                BOOST_ASSERT((*itbody)->GetEnv() == penv);
            }

            rapidjson::Value bodiesValue;
            bodiesValue.SetArray();

            FOREACHC(itBody, listbodies) {
                KinBodyPtr pBody = *itBody;
                rapidjson::Value bodyValue;

                // set dofvalues before serializing body info
                {
                    KinBody::KinBodyStateSaver saver(pBody);
                    vector<dReal> vZeros(pBody->GetDOF(), 0);
                    pBody->SetDOFValues(vZeros, KinBody::CLA_Nothing);
                    pBody->SetTransform(Transform()); // TODO: is this necessary

                    if (!pBody->IsRobot()) {
                        KinBody::KinBodyInfo info;
                        pBody->ExtractInfo(info);
                        info._referenceUri = _CanonicalizeURI(info._referenceUri);
                        info.SerializeJSON(bodyValue, _allocator, fUnitScale);
                    } else {
                        RobotBasePtr pRobot = RaveInterfaceCast<RobotBase>(pBody);
                        RobotBase::RobotBaseInfo info;
                        pRobot->ExtractInfo(info);
                        info._referenceUri = _CanonicalizeURI(info._referenceUri);
                        FOREACH(itConnectedBodyInfo, info._vConnectedBodyInfos) {
                            (*itConnectedBodyInfo)->_uri = _CanonicalizeURI((*itConnectedBodyInfo)->_uri);
                        }
                        info.SerializeJSON(bodyValue, _allocator, fUnitScale, _serializeOptions);
                    }
                }
                // dof value
                std::vector<dReal> vDOFValues;
                pBody->GetDOFValues(vDOFValues);
                if (vDOFValues.size() > 0) {
                    rapidjson::Value dofValues;
                    dofValues.SetArray();
                    dofValues.Reserve(vDOFValues.size(), _allocator);
                    for(size_t iDOF=0; iDOF<vDOFValues.size(); iDOF++) {
                        rapidjson::Value jointDOFValue;
                        KinBody::JointPtr pJoint = pBody->GetJointFromDOFIndex(iDOF);
                        std::string jointName = pJoint->GetName();
                        int jointAxis = iDOF - pJoint->GetDOFIndex();
                        OpenRAVE::orjson::SetJsonValueByKey(jointDOFValue, "jointName", jointName, _allocator);
                        OpenRAVE::orjson::SetJsonValueByKey(jointDOFValue, "jointAxis", jointAxis, _allocator);
                        OpenRAVE::orjson::SetJsonValueByKey(jointDOFValue, "value", vDOFValues[iDOF], _allocator);
                        dofValues.PushBack(jointDOFValue, _allocator);
                    }
                    OpenRAVE::orjson::SetJsonValueByKey(bodyValue, "dofValues", dofValues, _allocator);
                }

                OpenRAVE::orjson::SetJsonValueByKey(bodyValue, "transform", pBody->GetTransform(), _allocator);

                // grabbed info
                std::vector<KinBody::GrabbedInfoPtr> vGrabbedInfo;
                pBody->GetGrabbedInfo(vGrabbedInfo);
                if (vGrabbedInfo.size() > 0) {
                    rapidjson::Value grabbedsValue;
                    grabbedsValue.SetArray();
                    FOREACHC(itgrabbedinfo, vGrabbedInfo) {
                        rapidjson::Value grabbedValue;
                        (*itgrabbedinfo)->SerializeJSON(grabbedValue, _allocator, fUnitScale, _serializeOptions);
                        grabbedsValue.PushBack(grabbedValue, _allocator);
                    }
                    bodyValue.AddMember("grabbed", grabbedsValue, _allocator);
                }

                // finally push to the bodiesValue array if bodyValue is not empty
                if (bodyValue.MemberCount() > 0) {
                    bodiesValue.PushBack(bodyValue, _allocator);
                }
            }

            if (bodiesValue.Size() > 0) {
                _rEnvironment.AddMember("bodies", bodiesValue, _allocator);
            }
        }
    }

    std::string _CanonicalizeURI(const std::string& uri)
    {
        if (uri.empty()) {
            return uri;
        }
        std::string scheme, path, fragment;
        ParseURI(uri, scheme, path, fragment);

        if (_vForceResolveOpenRAVEScheme.size() > 0 && scheme == "file") {
            // check if inside an openrave path, and if so, return the openrave relative directory instead using "openrave:"
            std::string filename;
            if (RaveInvertFileLookup(filename, path)) {
                path = "/" + filename;
                scheme = _vForceResolveOpenRAVEScheme;
            }
        }

        // TODO: fix other scheme.

        // fix extension, replace dae with json
        // this is done for ease of migration
        if (RemoveSuffix(path, ".dae")) {
            path += ".json";
        }

        std::string newuri = scheme + ":" + path;
        if (fragment.size() > 0) {
            newuri += "#" + fragment;
        }
        return newuri;
    }

    std::string _vForceResolveOpenRAVEScheme; ///< if specified, writer will attempt to convert a local system URI (**file:/**) to a a relative path with respect to $OPENRAVE_DATA paths and use **customscheme** as the scheme

    int _serializeOptions; ///< the serialization options

    rapidjson::Value& _rEnvironment;
    rapidjson::Document::AllocatorType& _allocator;
};

void RaveWriteJSONFile(EnvironmentBasePtr penv, const std::string& filename, const AttributesList& atts, rapidjson::Document::AllocatorType& alloc)
{
    std::ofstream ofstream(filename.c_str());
    RaveWriteJSONStream(penv, ofstream, atts, alloc);
}

void RaveWriteJSONFile(const std::list<KinBodyPtr>& listbodies, const std::string& filename, const AttributesList& atts, rapidjson::Document::AllocatorType& alloc)
{
    std::ofstream ofstream(filename.c_str());
    RaveWriteJSONStream(listbodies, ofstream, atts, alloc);
}

void RaveWriteJSONStream(EnvironmentBasePtr penv, ostream& os, const AttributesList& atts, rapidjson::Document::AllocatorType& alloc)
{
    rapidjson::Document doc(&alloc);
    EnvironmentJSONWriter jsonwriter(atts, doc, doc.GetAllocator());
    jsonwriter.Write(penv);
    OpenRAVE::orjson::DumpJson(doc, os);
}

void RaveWriteJSONStream(const std::list<KinBodyPtr>& listbodies, ostream& os, const AttributesList& atts, rapidjson::Document::AllocatorType& alloc)
{
    rapidjson::Document doc(&alloc);
    EnvironmentJSONWriter jsonwriter(atts, doc, doc.GetAllocator());
    jsonwriter.Write(listbodies);
    OpenRAVE::orjson::DumpJson(doc, os);
}

void RaveWriteJSONMemory(EnvironmentBasePtr penv, std::vector<char>& output, const AttributesList& atts, rapidjson::Document::AllocatorType& alloc)
{
    rapidjson::Document doc(&alloc);
    EnvironmentJSONWriter jsonwriter(atts, doc, doc.GetAllocator());
    jsonwriter.Write(penv);
    OpenRAVE::orjson::DumpJson(doc, output);
}

void RaveWriteJSONMemory(const std::list<KinBodyPtr>& listbodies, std::vector<char>& output, const AttributesList& atts, rapidjson::Document::AllocatorType& alloc)
{
    rapidjson::Document doc(&alloc);
    EnvironmentJSONWriter jsonwriter(atts, doc, doc.GetAllocator());
    jsonwriter.Write(listbodies);
    OpenRAVE::orjson::DumpJson(doc, output);
}

void RaveWriteJSON(EnvironmentBasePtr penv, rapidjson::Value& rEnvironment, rapidjson::Document::AllocatorType& allocator, const AttributesList& atts)
{
    EnvironmentJSONWriter jsonwriter(atts, rEnvironment, allocator);
    jsonwriter.Write(penv);
}

void RaveWriteJSON(const std::list<KinBodyPtr>& listbodies, rapidjson::Value& rEnvironment, rapidjson::Document::AllocatorType& allocator, const AttributesList& atts)
{
    EnvironmentJSONWriter jsonwriter(atts, rEnvironment, allocator);
    jsonwriter.Write(listbodies);
}

void RaveWriteMsgPackFile(EnvironmentBasePtr penv, const std::string& filename, const AttributesList& atts, rapidjson::Document::AllocatorType& alloc)
{
    std::ofstream ofstream(filename.c_str());
    RaveWriteMsgPackStream(penv, ofstream, atts, alloc);
}

void RaveWriteMsgPackFile(const std::list<KinBodyPtr>& listbodies, const std::string& filename, const AttributesList& atts, rapidjson::Document::AllocatorType& alloc)
{
    std::ofstream ofstream(filename.c_str());
    RaveWriteMsgPackStream(listbodies, ofstream, atts, alloc);
}

void RaveWriteMsgPackStream(EnvironmentBasePtr penv, ostream& os, const AttributesList& atts, rapidjson::Document::AllocatorType& alloc)
{
    rapidjson::Document doc(&alloc);

    EnvironmentJSONWriter jsonwriter(atts, doc, doc.GetAllocator());
    jsonwriter.Write(penv);
    OpenRAVE::MsgPack::DumpMsgPack(doc, os);
}

void RaveWriteMsgPackStream(const std::list<KinBodyPtr>& listbodies, ostream& os, const AttributesList& atts, rapidjson::Document::AllocatorType& alloc)
{
    rapidjson::Document doc(&alloc);

    EnvironmentJSONWriter jsonwriter(atts, doc, doc.GetAllocator());
    jsonwriter.Write(listbodies);
    OpenRAVE::MsgPack::DumpMsgPack(doc, os);
}

void RaveWriteMsgPackMemory(EnvironmentBasePtr penv, std::vector<char>& output, const AttributesList& atts, rapidjson::Document::AllocatorType& alloc)
{
    rapidjson::Document doc(&alloc);
    EnvironmentJSONWriter jsonwriter(atts, doc, doc.GetAllocator());
    jsonwriter.Write(penv);
    OpenRAVE::MsgPack::DumpMsgPack(doc, output);
}

void RaveWriteMsgPackMemory(const std::list<KinBodyPtr>& listbodies, std::vector<char>& output, const AttributesList& atts, rapidjson::Document::AllocatorType& alloc)
{
    rapidjson::Document doc(&alloc);

    EnvironmentJSONWriter jsonwriter(atts, doc, doc.GetAllocator());
    jsonwriter.Write(listbodies);
    OpenRAVE::MsgPack::DumpMsgPack(doc, output);
}

void RaveWriteEncryptedFile(EnvironmentBasePtr penv, const std::string& filename, const AttributesList& atts, rapidjson::Document::AllocatorType& alloc, MimeType mimeType)
{
    std::ofstream ofs(filename, std::ios::binary);
    RaveWriteEncryptedStream(penv, ofs, atts, alloc, mimeType);
}

void RaveWriteEncryptedFile(const std::list<KinBodyPtr>& listbodies, const std::string& filename, const AttributesList& atts, rapidjson::Document::AllocatorType& alloc, MimeType mimeType)
{
    std::ofstream ofs(filename, std::ios::binary);
    RaveWriteEncryptedStream(listbodies, ofs, atts, alloc, mimeType);
}

void RaveWriteEncryptedMemory(EnvironmentBasePtr penv, std::vector<char>& output, const AttributesList& atts, rapidjson::Document::AllocatorType& alloc, MimeType mimeType)
{
    using vectorstream = ::boost::interprocess::basic_vectorstream<std::vector<char>, std::char_traits<char>>;
    vectorstream vs(std::ios::binary);
    RaveWriteEncryptedStream(penv, vs, atts, alloc, mimeType);
    vs.swap_vector(output);
}

void RaveWriteEncryptedMemory(const std::list<KinBodyPtr>& listbodies, std::vector<char>& output, const AttributesList& atts, rapidjson::Document::AllocatorType& alloc, MimeType mimeType)
{
    using vectorstream = ::boost::interprocess::basic_vectorstream<std::vector<char>, std::char_traits<char>>;
    vectorstream vs(std::ios::binary);
    RaveWriteEncryptedStream(listbodies, vs, atts, alloc, mimeType);
    vs.swap_vector(output);
}

void RaveWriteEncryptedStream(EnvironmentBasePtr penv, std::ostream& os, const AttributesList& atts, rapidjson::Document::AllocatorType& alloc, MimeType mimeType)
{
    std::string keyName;
    for (const std::pair<std::string, std::string>& attribute : atts) {
        if (attribute.first == "gpgkey") {
            keyName = attribute.second;
            break;
        }
    }

    std::string output_buffer;
    std::stringstream ss;
    switch (mimeType) {
    case MimeType::JSON: {
        RaveWriteJSONStream(penv, ss, atts, alloc);
        break;
    }
    case MimeType::MsgPack: {
        RaveWriteMsgPackStream(penv, ss, atts, alloc);
        break;
    }
    }
    ss.seekg(0, std::ios::beg);
    if (GpgEncrypt(ss, output_buffer, keyName)) {
        os.write(output_buffer.data(), output_buffer.size());
    } else {
        RAVELOG_ERROR("Failed to encrypt file, check GPG keys.");
    }
}

void RaveWriteEncryptedStream(const std::list<KinBodyPtr>& listbodies, std::ostream& os, const AttributesList& atts, rapidjson::Document::AllocatorType& alloc, MimeType mimeType)
{
    std::string keyName;
    for (const std::pair<std::string, std::string>& attribute : atts) {
        if (attribute.first == "gpgkey") {
            keyName = attribute.second;
            break;
        }
    }

    std::string output_buffer;
    std::stringstream ss;
    switch (mimeType) {
    case MimeType::JSON: {
        RaveWriteJSONStream(listbodies, ss, atts, alloc);
        break;
    }
    case MimeType::MsgPack: {
        RaveWriteMsgPackStream(listbodies, ss, atts, alloc);
        break;
    }
    }
    ss.seekg(0, std::ios::beg);
    if (GpgEncrypt(ss, output_buffer, keyName)) {
        os.write(output_buffer.data(), output_buffer.size());
    } else {
        RAVELOG_ERROR("Failed to encrypt file, check GPG keys.");
    }
}

}

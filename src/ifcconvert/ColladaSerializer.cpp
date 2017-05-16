/********************************************************************************
 *                                                                              *
 * This file is part of IfcOpenShell.                                           *
 *                                                                              *
 * IfcOpenShell is free software: you can redistribute it and/or modify         *
 * it under the terms of the Lesser GNU General Public License as published by  *
 * the Free Software Foundation, either version 3.0 of the License, or          *
 * (at your option) any later version.                                          *
 *                                                                              *
 * IfcOpenShell is distributed in the hope that it will be useful,              *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of               *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                 *
 * Lesser GNU General Public License for more details.                          *
 *                                                                              *
 * You should have received a copy of the Lesser GNU General Public License     *
 * along with this program. If not, see <http://www.gnu.org/licenses/>.         *
 *                                                                              *
 ********************************************************************************/

#ifdef WITH_OPENCOLLADA

#include "ColladaSerializer.h"

#include <COLLADASWPrimitves.h>
#include <COLLADASWSource.h>
#include <COLLADASWScene.h>
#include <COLLADASWNode.h>
#include <COLLADASWInstanceGeometry.h>
#include <COLLADASWBaseInputElement.h>
#include <COLLADASWAsset.h>

#include <string>
#include <cmath>

#include <boost/lexical_cast.hpp>
#include <boost/numeric/ublas/vector.hpp>
#include <boost/numeric/ublas/vector_proxy.hpp>
#include <boost/numeric/ublas/matrix.hpp>
#include <boost/numeric/ublas/triangular.hpp>
#include <boost/numeric/ublas/lu.hpp>
#include <boost/numeric/ublas/io.hpp>

using namespace IfcSchema;
using namespace boost::numeric::ublas;

static void collada_id(std::string &s)
{
    IfcUtil::sanitate_material_name(s);
    IfcUtil::escape_xml(s);
}

void ColladaSerializer::ColladaExporter::ColladaGeometries::addFloatSource(const std::string& mesh_id,
    const std::string& suffix, const std::vector<real_t>& floats, const char* coords /* = "XYZ" */)
{
	COLLADASW::FloatSource source(mSW);
	source.setId(mesh_id + suffix);
	source.setArrayId(mesh_id + suffix + COLLADASW::LibraryGeometries::ARRAY_ID_SUFFIX);
    const size_t num_elems = strlen(coords);
    source.setAccessorStride(static_cast<unsigned long>(num_elems));
    source.setAccessorCount(static_cast<unsigned long>(floats.size() / num_elems));
    for (size_t i = 0; i < num_elems; ++i) {
		source.getParameterNameList().push_back(std::string(1, coords[i]));
	}
	source.prepareToAppendValues();
    for (std::vector<real_t>::const_iterator it = floats.begin(); it != floats.end(); ++it) {
		source.appendValues(*it);
	}
	source.finish();
}

void ColladaSerializer::ColladaExporter::ColladaGeometries::write(
    const std::string &mesh_id, const std::string& default_material_name, const std::vector<real_t>& positions,
    const std::vector<real_t>& normals, const std::vector<int>& faces, const std::vector<int>& edges,
    const std::vector<int> material_ids, const std::vector<IfcGeom::Material>& materials,
    const std::vector<real_t>& uvs)
{
	openMesh(mesh_id);
	
	// The normals vector can be empty for example when the WELD_VERTICES setting is used.
	// IfcOpenShell does not provide them with multiple face normals collapsed into a single vertex.
	const bool has_normals = !normals.empty();
    const bool has_uvs = !uvs.empty();
	
	addFloatSource(mesh_id, COLLADASW::LibraryGeometries::POSITIONS_SOURCE_ID_SUFFIX, positions);
	if (has_normals) {
		addFloatSource(mesh_id, COLLADASW::LibraryGeometries::NORMALS_SOURCE_ID_SUFFIX, normals);
        if (has_uvs) {
            addFloatSource(mesh_id, COLLADASW::LibraryGeometries::TEXCOORDS_SOURCE_ID_SUFFIX, uvs, "UV");
        }
	}

	COLLADASW::VerticesElement vertices(mSW);
	vertices.setId(mesh_id + COLLADASW::LibraryGeometries::VERTICES_ID_SUFFIX );
	vertices.getInputList().push_back(COLLADASW::Input(COLLADASW::InputSemantic::POSITION, "#" + mesh_id + COLLADASW::LibraryGeometries::POSITIONS_SOURCE_ID_SUFFIX));
	vertices.add();
	
	std::vector<int>::const_iterator index_range_start = faces.begin();
	std::vector<int>::const_iterator material_it = material_ids.begin();
	int previous_material_id = -1;
	for (std::vector<int>::const_iterator it = faces.begin(); !faces.empty(); it += 3) {

		int current_material_id = 0;
		if (material_it != material_ids.end()) {
			// In order for the last range of equal material ids to be output as well, this loop iterates
			// one element past the end of the vector. This needs to be observed when incrementing.
			current_material_id = *(material_it++);
		}

		const size_t num_triangles = std::distance(index_range_start, it) / 3;
		if ((previous_material_id != current_material_id && num_triangles > 0) || (it == faces.end())) {
			COLLADASW::Triangles triangles(mSW);
            std::string material_name = (serializer->settings().get(SerializerSettings::USE_MATERIAL_NAMES)
                ? materials[previous_material_id].original_name() : materials[previous_material_id].name());
            collada_id(material_name);
            triangles.setMaterial(material_name);
            triangles.setCount((unsigned long)num_triangles);
			int offset = 0;
			triangles.getInputList().push_back(COLLADASW::Input(COLLADASW::InputSemantic::VERTEX,"#" + mesh_id + COLLADASW::LibraryGeometries::VERTICES_ID_SUFFIX, offset++));
			if (has_normals) {
				triangles.getInputList().push_back(COLLADASW::Input(COLLADASW::InputSemantic::NORMAL,"#" + mesh_id + COLLADASW::LibraryGeometries::NORMALS_SOURCE_ID_SUFFIX, offset++));
			}
            if (has_uvs) {
                triangles.getInputList().push_back(COLLADASW::Input(COLLADASW::InputSemantic::TEXCOORD,"#" + mesh_id + COLLADASW::LibraryGeometries::TEXCOORDS_SOURCE_ID_SUFFIX, offset++));
            }
			triangles.prepareToAppendValues();
			for (std::vector<int>::const_iterator jt = index_range_start; jt != it; ++jt) {
				const int idx = *jt;
                if (has_normals && has_uvs) {
                    triangles.appendValues(idx, idx, idx);
                } else if(has_normals) {
					triangles.appendValues(idx, idx);
				} else {
					triangles.appendValues(idx);
				}
			}
			triangles.finish();
			index_range_start = it;
		}
		previous_material_id = current_material_id;
		if (it == faces.end()) {
			break;
		}
	}

	std::set<int> faces_set (faces.begin(), faces.end());
	typedef std::vector< std::pair<int, std::vector<unsigned long> > > linelist_t;
	linelist_t linelist;

	int num_lines = 0;
	for ( std::vector<int>::const_iterator it = edges.begin(); it != edges.end(); ++num_lines) {
		const int i1 = *(it++);
		const int i2 = *(it++);

		if (faces_set.find(i1) != faces_set.end() || faces_set.find(i2) != faces_set.end()) {
			continue;
		}

		const int current_material_id = *(material_it++);
		if ((previous_material_id != current_material_id) || (num_lines == 0)) {
			linelist.resize(linelist.size() + 1);
		}

		linelist.rbegin()->second.push_back(i1);
		linelist.rbegin()->second.push_back(i2);
	}

	for (linelist_t::const_iterator it = linelist.begin(); it != linelist.end(); ++it) {
		COLLADASW::Lines lines(mSW);
        std::string material_name = (serializer->settings().get(SerializerSettings::USE_MATERIAL_NAMES)
            ? materials[it->first].original_name() : materials[it->first].name());
        collada_id(material_name);
        lines.setMaterial(material_name);
		lines.setCount((unsigned long)it->second.size());
		int offset = 0;
		lines.getInputList().push_back(COLLADASW::Input(COLLADASW::InputSemantic::VERTEX, "#" + mesh_id + COLLADASW::LibraryGeometries::VERTICES_ID_SUFFIX, offset));
		lines.prepareToAppendValues();
		lines.appendValues(it->second);
		lines.finish();
	}

	closeMesh();
	closeGeometry();
}

void ColladaSerializer::ColladaExporter::ColladaGeometries::close() {
	closeLibrary();
}

void ColladaSerializer::ColladaExporter::ColladaScene::add(
    const std::string& node_id, const std::string& node_name, const std::string& geom_name,
    const std::vector<std::string>& material_ids, const std::vector<real_t>& posmatrix)
{
	if (!scene_opened) {
		openVisualScene(scene_id);
		scene_opened = true;
	}
			
	COLLADASW::Node node(mSW);
	node.setNodeId(node_id);
	node.setNodeName(node_name);
	node.setType(COLLADASW::Node::NODE);

	// The matrix attribute of an entity is basically a 4x3 representation of its ObjectPlacement.
	// Note that this placement is absolute, ie it is multiplied with all parent placements.
	
	double matrix_array[4][4] = {
        { (double)posmatrix[0], (double)posmatrix[3], (double)posmatrix[6], (double)posmatrix[ 9] },
        { (double)posmatrix[1], (double)posmatrix[4], (double)posmatrix[7], (double)posmatrix[10] },
        { (double)posmatrix[2], (double)posmatrix[5], (double)posmatrix[8], (double)posmatrix[11] },
        { 0, 0, 0, 1 }
	};
	
	// If this is not the first parent, get the relative placement
	if (parentNodes.size() > 0)
	{
		double relative[4][4] = {
			{ 0, 0, 0, 0 },
			{ 0, 0, 0, 0 },
			{ 0, 0, 0, 0 },
			{ 0, 0, 0, 0 }
		};
		std::cout << "-------------------------------------------------------------------------------------------------------------------------------------\n";
		std::cout << "REPRESENTATION : " << node_name << " placement, using " << parentNodes.top()->getNodeName() << " ...\n";
		// Multiplication
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				for (int k = 0; k < 4; ++k) {
					relative[i][j] += matrix_array[i][k] * matrixStack.top()(k, j);
				}
			}
		}

		// Copy from relative to matrix_array
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				matrix_array[i][j] = relative[i][j];
			}
		}

	}
	
    matrix_array[0][3] += serializer->settings().offset[0];
    matrix_array[1][3] += serializer->settings().offset[1];
    matrix_array[2][3] += serializer->settings().offset[2];

	node.start();
	node.addMatrix(matrix_array);
	COLLADASW::InstanceGeometry instanceGeometry(mSW);
	instanceGeometry.setUrl ("#" + geom_name);
    foreach(std::string material_name, material_ids) {
        /// @todo This is done 6 times in this file, try to perform this once and be done with the material naming for the export.
        collada_id(material_name);
        COLLADASW::InstanceMaterial material (material_name, "#" + material_name);
		instanceGeometry.getBindMaterial().getInstanceMaterialList().push_back(material);
	}
	instanceGeometry.add();
	node.end();
}

void ColladaSerializer::ColladaExporter::ColladaScene::addParent(const IfcGeom::Element<real_t>& parent){
	//we open the visual scene tag if it's not.
	if (!scene_opened) {
		openVisualScene(scene_id);
		scene_opened = true;
	}


	const std::vector<real_t> parentMatrix = parent.transformation().matrix().data();
	
	double matrix_array[4][4] = {
		{ (double)parentMatrix[0], (double)parentMatrix[3], (double)parentMatrix[6], (double)parentMatrix[9] },
		{ (double)parentMatrix[1], (double)parentMatrix[4], (double)parentMatrix[7], (double)parentMatrix[10] },
		{ (double)parentMatrix[2], (double)parentMatrix[5], (double)parentMatrix[8], (double)parentMatrix[11] },
		{ 0, 0, 0, 1 }
	};
	
	// =========
	
	// If this is not the first parent, get the relative placement
	if (parentNodes.size() > 0)
	{
		double relative[4][4] = {
			{ 0, 0, 0, 0 },
			{ 0, 0, 0, 0 },
			{ 0, 0, 0, 0 },
			{ 0, 0, 0, 0 }
		};
		std::cout << "-------------------------------------------------------------------------------------------------------------------------------------\n";
		std::cout << "PARENT         : " << parent.name() << "_" << parent.type() << " placement, using " << parentNodes.top()->getNodeName() << " ...\n";
		// Multiplication
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				for (int k = 0; k < 4; ++k) {
					relative[i][j] += matrix_array[i][k] * matrixStack.top()(k, j);
				}
			}
		}

		// TODO : Handle the case where there was no inverse

		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				matrix_array[i][j] = relative[i][j];
			}
		}		
	}
	
	//adding the offset to the matrix
	//matrix_array[0][3] += serializer->settings().offset[0];
	//matrix_array[1][3] += serializer->settings().offset[1];
	//matrix_array[2][3] += serializer->settings().offset[2];
	
	
	const std::string id = "representation-" + boost::lexical_cast<std::string>(parent.id());

	COLLADASW::Node *current_node;
	current_node = new COLLADASW::Node(mSW);
	current_node->setNodeId(id);
	current_node->setNodeName(parent.type() + " " + parent.name());
	current_node->setType(COLLADASW::Node::NODE);
	current_node->start();
	current_node->addMatrix(matrix_array);

	// ==== Inverse the matrix and save it ====

	double m[4][4] = {
		{ (double)parentMatrix[0], (double)parentMatrix[3], (double)parentMatrix[6], (double)parentMatrix[9] },
		{ (double)parentMatrix[1], (double)parentMatrix[4], (double)parentMatrix[7], (double)parentMatrix[10] },
		{ (double)parentMatrix[2], (double)parentMatrix[5], (double)parentMatrix[8], (double)parentMatrix[11] },
		{ 0, 0, 0, 1 }
	};

	double det =
		m[0][3] * m[1][2] * m[2][1] * m[3][0] - m[0][2] * m[1][3] * m[2][1] * m[3][0] - m[0][3] * m[1][1] * m[2][2] * m[3][0] + m[0][1] * m[1][3] * m[2][2] * m[3][0] +
		m[0][2] * m[1][1] * m[2][3] * m[3][0] - m[0][1] * m[1][2] * m[2][3] * m[3][0] - m[0][3] * m[1][2] * m[2][0] * m[3][1] + m[0][2] * m[1][3] * m[2][0] * m[3][1] +
		m[0][3] * m[1][0] * m[2][2] * m[3][1] - m[0][0] * m[1][3] * m[2][2] * m[3][1] - m[0][2] * m[1][0] * m[2][3] * m[3][1] + m[0][0] * m[1][2] * m[2][3] * m[3][1] +
		m[0][3] * m[1][1] * m[2][0] * m[3][2] - m[0][1] * m[1][3] * m[2][0] * m[3][2] - m[0][3] * m[1][0] * m[2][1] * m[3][2] + m[0][0] * m[1][3] * m[2][1] * m[3][2] +
		m[0][1] * m[1][0] * m[2][3] * m[3][2] - m[0][0] * m[1][1] * m[2][3] * m[3][2] - m[0][2] * m[1][1] * m[2][0] * m[3][3] + m[0][1] * m[1][2] * m[2][0] * m[3][3] +
		m[0][2] * m[1][0] * m[2][1] * m[3][3] - m[0][0] * m[1][2] * m[2][1] * m[3][3] - m[0][1] * m[1][0] * m[2][2] * m[3][3] + m[0][0] * m[1][1] * m[2][2] * m[3][3];


	if (det != 0)
	{
		double inverse[4][4];
		inverse[0][0] = m[1][2] * m[2][3] * m[3][1] - m[1][3] * m[2][2] * m[3][1] + m[1][3] * m[2][1] * m[3][2] - m[1][1] * m[2][3] * m[3][2] - m[1][2] * m[2][1] * m[3][3] + m[1][1] * m[2][2] * m[3][3];
		inverse[0][1] = m[0][3] * m[2][2] * m[3][1] - m[0][2] * m[2][3] * m[3][1] - m[0][3] * m[2][1] * m[3][2] + m[0][1] * m[2][3] * m[3][2] + m[0][2] * m[2][1] * m[3][3] - m[0][1] * m[2][2] * m[3][3];
		inverse[0][2] = m[0][2] * m[1][3] * m[3][1] - m[0][3] * m[1][2] * m[3][1] + m[0][3] * m[1][1] * m[3][2] - m[0][1] * m[1][3] * m[3][2] - m[0][2] * m[1][1] * m[3][3] + m[0][1] * m[1][2] * m[3][3];
		inverse[0][3] = m[0][3] * m[1][2] * m[2][1] - m[0][2] * m[1][3] * m[2][1] - m[0][3] * m[1][1] * m[2][2] + m[0][1] * m[1][3] * m[2][2] + m[0][2] * m[1][1] * m[2][3] - m[0][1] * m[1][2] * m[2][3];

		inverse[1][0] = m[1][3] * m[2][2] * m[3][0] - m[1][2] * m[2][3] * m[3][0] - m[1][3] * m[2][0] * m[3][2] + m[1][0] * m[2][3] * m[3][2] + m[1][2] * m[2][0] * m[3][3] - m[1][0] * m[2][2] * m[3][3];
		inverse[1][1] = m[0][2] * m[2][3] * m[3][0] - m[0][3] * m[2][2] * m[3][0] + m[0][3] * m[2][0] * m[3][2] - m[0][0] * m[2][3] * m[3][2] - m[0][2] * m[2][0] * m[3][3] + m[0][0] * m[2][2] * m[3][3];
		inverse[1][2] = m[0][3] * m[1][2] * m[3][0] - m[0][2] * m[1][3] * m[3][0] - m[0][3] * m[1][0] * m[3][2] + m[0][0] * m[1][3] * m[3][2] + m[0][2] * m[1][0] * m[3][3] - m[0][0] * m[1][2] * m[3][3];
		inverse[1][3] = m[0][2] * m[1][3] * m[2][0] - m[0][3] * m[1][2] * m[2][0] + m[0][3] * m[1][0] * m[2][2] - m[0][0] * m[1][3] * m[2][2] - m[0][2] * m[1][0] * m[2][3] + m[0][0] * m[1][2] * m[2][3];

		inverse[2][0] = m[1][1] * m[2][3] * m[3][0] - m[1][3] * m[2][1] * m[3][0] + m[1][3] * m[2][0] * m[3][1] - m[1][0] * m[2][3] * m[3][1] - m[1][1] * m[2][0] * m[3][3] + m[1][0] * m[2][1] * m[3][3];
		inverse[2][1] = m[0][3] * m[2][1] * m[3][0] - m[0][1] * m[2][3] * m[3][0] - m[0][3] * m[2][0] * m[3][1] + m[0][0] * m[2][3] * m[3][1] + m[0][1] * m[2][0] * m[3][3] - m[0][0] * m[2][1] * m[3][3];
		inverse[2][2] = m[0][1] * m[1][3] * m[3][0] - m[0][3] * m[1][1] * m[3][0] + m[0][3] * m[1][0] * m[3][1] - m[0][0] * m[1][3] * m[3][1] - m[0][1] * m[1][0] * m[3][3] + m[0][0] * m[1][1] * m[3][3];
		inverse[2][3] = m[0][3] * m[1][1] * m[2][0] - m[0][1] * m[1][3] * m[2][0] - m[0][3] * m[1][0] * m[2][1] + m[0][0] * m[1][3] * m[2][1] + m[0][1] * m[1][0] * m[2][3] - m[0][0] * m[1][1] * m[2][3];

		inverse[3][0] = m[1][2] * m[2][1] * m[3][0] - m[1][1] * m[2][2] * m[3][0] - m[1][2] * m[2][0] * m[3][1] + m[1][0] * m[2][2] * m[3][1] + m[1][1] * m[2][0] * m[3][2] - m[1][0] * m[2][1] * m[3][2];
		inverse[3][1] = m[0][1] * m[2][2] * m[3][0] - m[0][2] * m[2][1] * m[3][0] + m[0][2] * m[2][0] * m[3][1] - m[0][0] * m[2][2] * m[3][1] - m[0][1] * m[2][0] * m[3][2] + m[0][0] * m[2][1] * m[3][2];
		inverse[3][2] = m[0][2] * m[1][1] * m[3][0] - m[0][1] * m[1][2] * m[3][0] - m[0][2] * m[1][0] * m[3][1] + m[0][0] * m[1][2] * m[3][1] + m[0][1] * m[1][0] * m[3][2] - m[0][0] * m[1][1] * m[3][2];
		inverse[3][3] = m[0][1] * m[1][2] * m[2][0] - m[0][2] * m[1][1] * m[2][0] + m[0][2] * m[1][0] * m[2][1] - m[0][0] * m[1][2] * m[2][1] - m[0][1] * m[1][0] * m[2][2] + m[0][0] * m[1][1] * m[2][2];


		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				inverse[i][j] /= det;
			}
		}

		/*
		std::cout << "matrix array : \n";
		std::cout << m[0][0] << " | " << m[0][1] << " | " << m[0][2] << " | " << m[0][3] << " \n ";
		std::cout << m[1][0] << " | " << m[1][1] << " | " << m[1][2] << " | " << m[1][3] << " \n ";
		std::cout << m[2][0] << " | " << m[2][1] << " | " << m[2][2] << " | " << m[2][3] << " \n ";
		std::cout << m[3][0] << " | " << m[3][1] << " | " << m[3][2] << " | " << m[3][3] << " \n ";
		*/
		matrix<double> save(4, 4);
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				save(i, j) = 0;
			}
		}
		
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				save(i, j) = inverse[i][j];
			}
		}
		
		matrixStack.push(save);
	}
	else
	{
		std::cout << "couldn't inverse the position matrix \n";
	}

	// Add the node to the parent stack
	parentNodes.push(current_node);
	serializer->parentStackId.push(parent.id());

}

void ColladaSerializer::ColladaExporter::ColladaScene::closeParent()
{
	// Get the top element
	COLLADASW::Node *current_node = parentNodes.top();

	// Close the node
	current_node->end();

	// Remove it from the stack
	parentNodes.pop();
	matrixStack.pop();
	serializer->parentStackId.pop();

	// Free the memory
	delete current_node;
	current_node = NULL;
}

void ColladaSerializer::ColladaExporter::ColladaScene::write() {
	if (scene_opened) {
		closeVisualScene();
		closeLibrary();
		
		COLLADASW::Scene scene (mSW, COLLADASW::URI ("#" + scene_id));
		scene.add();		
	}
}

void ColladaSerializer::ColladaExporter::ColladaMaterials::ColladaEffects::write(const IfcGeom::Material& material)
{
    std::string material_name = (serializer->settings().get(SerializerSettings::USE_MATERIAL_NAMES)
        ? material.original_name() : material.name());
    collada_id(material_name);
    openEffect(material_name + "-fx");
	COLLADASW::EffectProfile effect(mSW);
	effect.setShaderType(COLLADASW::EffectProfile::LAMBERT);
	if (material.hasDiffuse()) {
		const double* diffuse = material.diffuse();
		effect.setDiffuse(COLLADASW::ColorOrTexture(COLLADASW::Color(diffuse[0],diffuse[1],diffuse[2])));
	}
	if (material.hasSpecular()) {
		const double* specular = material.specular();
		effect.setSpecular(COLLADASW::ColorOrTexture(COLLADASW::Color(specular[0],specular[1],specular[2])));
	}
	if (material.hasSpecularity()) {
		effect.setShininess(material.specularity());
	}
	if (material.hasTransparency()) {
		const double transparency = material.transparency();
		if (transparency > 0) {
			// The default opacity mode for Collada is A_ONE, which apparently indicates a
			// transparency value of 1 to be fully opaque. Hence transparency is inverted.
			effect.setTransparency(1.0 - transparency);
		}
	}
	addEffectProfile(effect);
	closeEffect();
}

void ColladaSerializer::ColladaExporter::ColladaMaterials::ColladaEffects::close() {
	closeLibrary();
}

void ColladaSerializer::ColladaExporter::ColladaMaterials::add(const IfcGeom::Material& material) {
	if (!contains(material)) {
		effects.write(material);
		materials.push_back(material);
	}
}

bool ColladaSerializer::ColladaExporter::ColladaMaterials::contains(const IfcGeom::Material& material) {
	return std::find(materials.begin(), materials.end(), material) != materials.end();
}

void ColladaSerializer::ColladaExporter::ColladaMaterials::write() {
	effects.close();
    foreach(const IfcGeom::Material& material, materials) {
        std::string material_name = (serializer->settings().get(SerializerSettings::USE_MATERIAL_NAMES)
            ? material.original_name() : material.name());
        std::string  material_name_unescaped = material_name; // workaround double-escaping that would occur in addInstanceEffect()
        IfcUtil::sanitate_material_name(material_name_unescaped);
        collada_id(material_name);
		openMaterial(material_name);
        addInstanceEffect("#" + material_name_unescaped + "-fx");
		closeMaterial();
	}
	closeLibrary();
}

void ColladaSerializer::ColladaExporter::startDocument(const std::string& unit_name, float unit_magnitude) {
	stream.startDocument();

	COLLADASW::Asset asset(&stream);
	asset.getContributor().mAuthoringTool = std::string("IfcOpenShell ") + IFCOPENSHELL_VERSION;
	asset.setUnit(unit_name, unit_magnitude);
	asset.setUpAxisType(COLLADASW::Asset::Z_UP);
	asset.add();
}

void ColladaSerializer::ColladaExporter::write(const IfcGeom::TriangulationElement<real_t>* o)
{
	const IfcGeom::Representation::Triangulation<real_t>& mesh = o->geometry();
	
	std::string slabSuffix = "";
	if (o->type() == "IfcSlab")
	{
		slabSuffix = differentiateSlabTypes(o);
	}
	
	const std::string name = serializer->settings().get(SerializerSettings::USE_ELEMENT_GUIDS) ?
		o->guid() : (serializer->settings().get(SerializerSettings::USE_ELEMENT_NAMES) ?
			o->name() : (serializer->settings().get(SerializerSettings::USE_ELEMENT_TYPES) ? o->type() + std::to_string(o->id()) + slabSuffix : o->unique_id()));
	const std::string representation_id = "representation-" + boost::lexical_cast<std::string>(o->geometry().id());
	std::vector<std::string> material_references;
	foreach(const IfcGeom::Material& material, mesh.materials()) {
		if (!materials.contains(material)) {
			materials.add(material);
		}
		std::string material_name = (serializer->settings().get(SerializerSettings::USE_MATERIAL_NAMES)
			? material.original_name() : material.name());
		collada_id(material_name);
		material_references.push_back(material_name);
	}

	DeferredObject defered = (serializer->settings().get(SerializerSettings::USE_ELEMENT_HIERARCHY) ?
		DeferredObject(name, representation_id, o->type(), o->transformation().matrix().data(), mesh.verts(), mesh.normals(), 
			mesh.faces(), mesh.edges(), mesh.material_ids(), mesh.materials(), material_references, mesh.uvs(), o->parents()) :
		DeferredObject(name, representation_id, o->type(), o->transformation().matrix().data(), mesh.verts(), mesh.normals(),
				mesh.faces(), mesh.edges(), mesh.material_ids(), mesh.materials(), material_references, mesh.uvs()));
	deferreds.push_back(defered);
}

std::string ColladaSerializer::ColladaExporter::differentiateSlabTypes(const IfcGeom::TriangulationElement<real_t>* o) {
	IfcSlab* slab = (IfcSlab*)o->product();
	switch (slab->PredefinedType())
	{
		case (IfcSlabTypeEnum::IfcSlabType_FLOOR):
			return "_Floor";
			break;
		case (IfcSlabTypeEnum::IfcSlabType_ROOF):
			return "_Roof";
			break;
		case (IfcSlabTypeEnum::IfcSlabType_LANDING):
			return "_Landing";
			break;
		case (IfcSlabTypeEnum::IfcSlabType_BASESLAB):
			return "_BaseSlab";
			break;
		case (IfcSlabTypeEnum::IfcSlabType_USERDEFINED):
			return "_" + slab->ObjectType();
			break;
		case (IfcSlabTypeEnum::IfcSlabType_NOTDEFINED):
			return "_NotDefined";
			break;
		default:
			return "_Unknown";
			break;
	}
}

void ColladaSerializer::ColladaExporter::endDocument() {
	// In fact due the XML based nature of Collada and its dependency on library nodes,
	// only at this point all objects are written to the stream.
	materials.write();
	bool use_hierarchy = serializer->settings().get(SerializerSettings::USE_ELEMENT_HIERARCHY);
	
	std::set<std::string> geometries_written;

	//if the setting USE_ELEMENT_HIERARCHY is in use, we sort the deferreds objects by their parents.
	
	if (use_hierarchy) {
		std::sort(deferreds.begin(), deferreds.end());
	}
	
	for (std::vector<DeferredObject>::const_iterator it = deferreds.begin(); it != deferreds.end(); ++it) {
		if (geometries_written.find(it->representation_id) != geometries_written.end()) {
			continue;
		}
		geometries_written.insert(it->representation_id);
		geometries.write(it->representation_id, it->type, it->vertices, it->normals, it->faces, it->edges, it->material_ids, it->materials, it->uvs);
	}
	geometries.close();

	int parent_id = -1;
	bool is_parent_tag_opened = false; 
	for (std::vector<DeferredObject>::const_iterator it = deferreds.begin(); it != deferreds.end(); ++it){
		const std::string object_name = it->unique_id;

		/*
		std::cout << "******************************\n";
		std::cout << "== " << it->unique_id << " ==\n";
		std::cout << " has " << it->parents.size() << " parents \n";
		for (unsigned i = 0; i < it->parents.size(); i++)
		{
			std::cout << "=== " << it->parents.at(i)->name() << " | " << it->parents.at(i)->id() << " ===\n";
		}
		*/

		// TODO : handle the case where a representation object has no parent (Can this really happen ?)
		if (use_hierarchy)
		{
			unsigned parentsNumber = it->parents.size();
			bool finished = false;
			while (!finished)
			{
				// If we need to add a parent
				if (serializer->parentStackId.size() <= parentsNumber)
				{
					if (serializer->parentStackId.empty()) { scene.addParent(*(it->parents.at(0))); }
					else
					{
						unsigned diff = parentsNumber - serializer->parentStackId.size();

						// If we have the wrong parent in the list
						if (serializer->parentStackId.top() != it->parents.at(parentsNumber - diff - 1)->id())
						{
							scene.closeParent();
						}
						// So far we have the right parents, we just need to add the missing ones
						else
						{
							for (unsigned i = parentsNumber - diff; i < parentsNumber; i++) { scene.addParent(*(it->parents.at(i))); }

							// if diff == 0, we can leave the loop. In fact we have the right number of parents, and the last one is ok
							if (diff == 0) { finished = true; }
						}
					}
				}
				// IF serializer->parentStackId.size() > parentsNumber
				else
				{
					// Close the finished nodes. After this we get the first case (serializer->parentStackId.size() <= parentsNumber)
					while (serializer->parentStackId.size() > parentsNumber) { scene.closeParent(); }
				}
			}
		}
		
        /// @todo redundant information using ID as both ID and Name, maybe omit Name or allow specifying what would be used as the name
		scene.add(object_name, object_name, it->representation_id, it->material_references, it->matrix);
	}

	//close the remaining parent tags.
	while (serializer->parentStackId.size() > 0) { scene.closeParent(); }

	scene.write();
	stream.endDocument();
}

bool ColladaSerializer::ready() {
	return true;
}

void ColladaSerializer::writeHeader() {
	exporter.startDocument(unit_name, unit_magnitude);
}

void ColladaSerializer::write(const IfcGeom::TriangulationElement<real_t>* o) {
    exporter.write(o);
}


void ColladaSerializer::finalize() {
	exporter.endDocument();
}

#endif

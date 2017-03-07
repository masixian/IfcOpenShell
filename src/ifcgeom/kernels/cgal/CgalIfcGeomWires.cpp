#include "CgalKernel.h"

bool IfcGeom::CgalKernel::convert(const IfcSchema::IfcPolyLoop* l, cgal_wire_t& result) {
  IfcSchema::IfcCartesianPoint::list::ptr points = l->Polygon();
  
  // Parse and store the points in a sequence
  cgal_wire_t polygon = std::vector<Kernel::Point_3>();
  for(IfcSchema::IfcCartesianPoint::list::it it = points->begin(); it != points->end(); ++ it) {
    cgal_point_t pnt;
    IfcGeom::CgalKernel::convert(*it, pnt);
    polygon.push_back(pnt);
  }
  
  // A loop should consist of at least three vertices
  std::size_t original_count = polygon.size();
  if (original_count < 3) {
    Logger::Message(Logger::LOG_ERROR, "Not enough edges for:", l->entity);
    return false;
  }
  
  // Remove points that are too close to one another
  remove_duplicate_points_from_loop(polygon, true);
  
  std::size_t count = polygon.size();
  if (original_count - count != 0) {
    std::stringstream ss; ss << (original_count - count) << " edges removed for:";
    Logger::Message(Logger::LOG_WARNING, ss.str(), l->entity);
  }
  
  if (count < 3) {
    Logger::Message(Logger::LOG_ERROR, "Not enough edges for:", l->entity);
    return false;
  }
  
  result = polygon;
  
  //  std::cout << "PolyLoop: " << std::endl;
  //  for (auto &point: polygon) {
  //    std::cout << "\tPoint(" << point << ")" << std::endl;
  //  }
  
  return true;
}

bool IfcGeom::CgalKernel::convert(const IfcSchema::IfcPolyline* l, cgal_wire_t& result) {
  IfcSchema::IfcCartesianPoint::list::ptr points = l->Points();
  
  // Parse and store the points in a sequence
  cgal_wire_t polygon = std::vector<Kernel::Point_3>();
  for(IfcSchema::IfcCartesianPoint::list::it it = points->begin(); it != points->end(); ++ it) {
    cgal_point_t pnt;
    IfcGeom::CgalKernel::convert(*it, pnt);
    polygon.push_back(pnt);
  }
  
  // Remove points that are too close to one another
  remove_duplicate_points_from_loop(polygon, false);
  
  result = polygon;
  return true;
}

bool IfcGeom::CgalKernel::convert(const IfcSchema::IfcEdgeLoop* l, cgal_wire_t& result) {
  IfcSchema::IfcOrientedEdge::list::ptr li = l->EdgeList();
  cgal_wire_t mw;
  for (IfcSchema::IfcOrientedEdge::list::it it = li->begin(); it != li->end(); ++it) {
    cgal_wire_t w;
    if (convert_wire(*it, w)) {
      // TODO: What to do here? Add some points only?
//      mw.Add(TopoDS::Edge(TopoDS_Iterator(w).Value()));
      return false;
    }
  }
  result = mw;
  return true;
}

bool IfcGeom::CgalKernel::convert(const IfcSchema::IfcOrientedEdge* l, cgal_wire_t& result) {
  if (convert_wire(l->EdgeElement(), result)) {
    if (!l->Orientation()) {
      std::reverse(result.begin(),result.end());
    }
    return true;
  } else {
    return false;
  }
}

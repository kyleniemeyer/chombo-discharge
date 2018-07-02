/*!
  @file   ply_reader.cpp
  @brief  Implementation of ply_reader.H
  @author Robert Marskar
  @date   Apr. 2018
*/

#include "ply_reader.H"
#include "dcel_iterator.H"

#include <ParmParse.H>
#include <iostream>
#include <fstream>

void ply_reader::read_ascii(dcel_mesh& a_mesh, const std::string a_filename){
  std::ifstream filestream(a_filename);

  if(filestream.is_open()){
    Vector<RefCountedPtr<dcel_vert> >& vertices = a_mesh.get_vertices();
    Vector<RefCountedPtr<dcel_edge> >& edges    = a_mesh.get_edges();
    Vector<RefCountedPtr<dcel_poly> >& polygons = a_mesh.get_polygons();

    vertices.resize(0);
    edges.resize(0);
    polygons.resize(0);

    int num_vertices;  // Number of vertices
    int num_polygons;  // Number of polygons

    ply_reader::read_ascii_header(num_vertices, num_polygons, filestream); 
    ply_reader::read_ascii_vertices(vertices, num_vertices, filestream);
    ply_reader::read_ascii_polygons(polygons, edges, vertices, num_polygons, filestream);

    a_mesh.sanity_check();
  
    filestream.close();
  }
  else{
    const std::string error = "ply_reader::read_ascii - ERROR! Could not open file " + a_filename;
    MayDay::Abort(error.c_str());
  }
}

void ply_reader::read_ascii_header(int& a_num_vertices,
				   int& a_num_polygons,
				   std::ifstream& a_inputstream){

  std::string str1;
  std::string str2;
  std::string line;

  // Get number of vertices
  a_inputstream.clear();
  a_inputstream.seekg(0);
  while (getline(a_inputstream, line)){
    std::stringstream sstream(line);
    sstream >> str1 >> str2 >> a_num_vertices;
    if(str1 == "element" && str2 == "vertex"){
      break;
    }
  }

  // Get number of polygons
  a_inputstream.clear();
  a_inputstream.seekg(0);
  while (getline(a_inputstream, line)){
    std::stringstream sstream(line);
    sstream >> str1 >> str2 >> a_num_polygons;
    if(str1 == "element" && str2 == "face"){
      break;
    }
  }

  // Find the line # containing "end_header" halt the input stream there
  a_inputstream.clear();
  a_inputstream.seekg(0);
  while (getline(a_inputstream, line)){
    std::stringstream sstream(line);
    sstream >> str1;
    if(str1 == "end_header"){
      break;
    }
  }
}

void ply_reader::read_ascii_vertices(Vector<RefCountedPtr<dcel_vert> >& a_vertices,
				     const int a_num_vertices,
				     std::ifstream& a_inputstream){

  RealVect pos;
  Real& x = pos[0];
  Real& y = pos[1];
  Real& z = pos[2];

  RealVect norm;
  Real& nx = norm[0];
  Real& ny = norm[1];
  Real& nz = norm[2];
  
  int num = 0;

  std::string line;
  while(std::getline(a_inputstream, line)){
    num++;
    std::stringstream sstream(line);
    sstream >> x >> y >> z >> nx >> ny >> nz;

    RefCountedPtr<dcel_vert> vert = RefCountedPtr<dcel_vert> (new dcel_vert(pos));
    vert->set_normal(norm);
    a_vertices.push_back(vert);
    if(num == a_num_vertices){
      break;
    }
  } 
}

void ply_reader::read_ascii_polygons(Vector<RefCountedPtr<dcel_poly> >& a_polygons,
				     Vector<RefCountedPtr<dcel_edge> >& a_edges,
				     Vector<RefCountedPtr<dcel_vert> >& a_vertices,
				     const int a_num_polygons,
				     std::ifstream& a_inputstream){
  int num_vert;
  Vector<int> which_vertices;

  std::string line;
  int counter = 0;
  while(std::getline(a_inputstream, line)){
    counter++;
    
    std::stringstream sstream(line);

    sstream >> num_vert;
    which_vertices.resize(num_vert);
    for (int i = 0; i < num_vert; i++){
      sstream >> which_vertices[i];
    }

    // Build polygon and inside edges
    RefCountedPtr<dcel_poly> polygon = RefCountedPtr<dcel_poly> (new dcel_poly());

    // Get vertices. Add a reference to the newly created polygon
    Vector<RefCountedPtr<dcel_vert> > poly_vertices(num_vert);
    for (int i = 0; i < num_vert; i++){
      poly_vertices[i] = a_vertices[which_vertices[i]];
    }

    // Build inside edges. Polygon gets a reference to the edge
    Vector<RefCountedPtr<dcel_edge> > poly_edges(num_vert);
    for (int i = 0; i < num_vert; i++){
      poly_edges[i] = RefCountedPtr<dcel_edge> (new dcel_edge());
      poly_edges[i]->set_vert(poly_vertices[(i+1)%num_vert]);
    }
    polygon->set_edge(poly_edges[0]);

    // Associate prev/next
    for (int i = 0; i < num_vert; i++){
      poly_edges[i]->set_next(poly_edges[(i+1)%num_vert]);
      poly_edges[(i+1)%num_vert]->set_prev(poly_edges[i]);
    }

    // Set edges emanating from vertices if that hasn't been done already
    for (int i = 0; i < poly_vertices.size(); i++){
      if(poly_vertices[i]->get_edge().isNull()){
	poly_vertices[i]->set_edge(poly_edges[i]);
      }
    }

    // Check for pairs
    for (int i = 0; i < poly_edges.size(); i++){

      RefCountedPtr<dcel_edge>& edge = poly_edges[i];
      RefCountedPtr<dcel_vert>& vert = edge->get_vert();

      // Get all polygons connected to the current vertex and look for edge pairs
      Vector<RefCountedPtr<dcel_poly> >& polygons = vert->get_polycache();

      for (int j = 0; j < polygons.size(); j++){
	RefCountedPtr<dcel_edge>& other_polygon_edge = polygons[j]->get_edge();

	for (edge_iterator iter(*polygons[j]); iter.ok(); ++iter){
	  RefCountedPtr<dcel_edge>& other_polygon_edge = iter();

	  if(other_polygon_edge->get_vert() == edge->get_prev()->get_vert()){
	    edge->set_pair(other_polygon_edge);
	    other_polygon_edge->set_pair(edge);
	  }
	}
      }
    }

    // Add reference to newly created polygon
    for (int i = 0; i < poly_vertices.size(); i++){
      poly_vertices[i]->add_polygon(polygon);
      CH_assert(!poly_vertices[i]->get_edge().isNull());
    }

    // Add edges and polygons
    for (int i = 0; i < poly_edges.size(); i++){
      a_edges.push_back(poly_edges[i]);
    }
    a_polygons.push_back(polygon);
    
    if(counter == a_num_polygons){
      break;
    }
  }
}
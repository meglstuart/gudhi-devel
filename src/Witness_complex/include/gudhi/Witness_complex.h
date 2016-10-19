/*    This file is part of the Gudhi Library. The Gudhi library
 *    (Geometric Understanding in Higher Dimensions) is a generic C++
 *    library for computational topology.
 *
 *    Author(s):       Siargey Kachanovich
 *
 *    Copyright (C) 2015  INRIA (France)
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef WITNESS_COMPLEX_H_
#define WITNESS_COMPLEX_H_

#include <gudhi/Active_witness/Active_witness.h>
#include <gudhi/Kd_tree_search.h>

#include <utility>
#include <vector>
#include <list>
#include <limits>

namespace gss = Gudhi::spatial_searching;

namespace Gudhi {
  
namespace witness_complex {

/**
 * \private
 * \class Witness_complex
 * \brief Constructs (weak) witness complex for the given sets of witnesses and landmarks.
 * \ingroup witness_complex
 *
 * \tparam Kernel_ requires a <a target="_blank"
 * href="http://doc.cgal.org/latest/Kernel_d/classCGAL_1_1Epick__d.html">CGAL::Epick_d</a> class, which
 * can be static if you know the ambiant dimension at compile-time, or dynamic if you don't.
*/
template< class Kernel_ >
class Witness_complex {
private:
  typedef Kernel_                                                 K;
  typedef typename K::Point_d                                     Point_d;
  typedef typename K::FT                                          FT;
  typedef std::vector<Point_d>                                    Point_range;
  typedef gss::Kd_tree_search<Kernel_, Point_range>               Kd_tree;
  typedef typename Kd_tree::INS_range                             Nearest_landmark_range;
  typedef typename std::vector<Nearest_landmark_range>            Nearest_landmark_table;
  typedef typename Nearest_landmark_range::iterator               Nearest_landmark_row_iterator;
  
  typedef std::vector< double > Point_t;
  typedef std::vector< Point_t > Point_Vector;

  typedef FT Filtration_value;

  
  typedef std::size_t Witness_id;
  typedef typename Nearest_landmark_range::Point_with_transformed_distance Id_distance_pair;
  typedef typename Id_distance_pair::first_type Landmark_id;
  typedef Active_witness<Id_distance_pair, Nearest_landmark_range> ActiveWitness;
  typedef std::list< ActiveWitness > ActiveWitnessList;
  typedef std::vector< Landmark_id > typeVectorVertex;
  typedef std::pair< typeVectorVertex, Filtration_value> typeSimplex;

  typedef Landmark_id Vertex_handle;

 private:
  Point_range                         witnesses_, landmarks_;
  Kd_tree                             landmark_tree_;
  
 public:
  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  /* @name Constructor
   */

  //@{

  /**
   *  \brief Initializes member variables before constructing simplicial complex.
   *  \details Records landmarks from the range 'landmarks' into a 
   *           table internally, as well as witnesses from the range 'witnesses'.
   */
  template< typename LandmarkRange,
            typename WitnessRange >
  Witness_complex(const LandmarkRange & landmarks,
                  const WitnessRange &  witnesses)
    : witnesses_(witnesses), landmarks_(landmarks), landmark_tree_(landmarks_)
  {    
  }

  /** \brief Returns the point corresponding to the given vertex.
   *  @param[in] vertex Vertex handle of the point to retrieve.
   */
  Point_d get_point( Vertex_handle vertex ) const
  {
    return landmarks_[vertex];
  }
  
  /** \brief Outputs the (weak) witness complex of relaxation 'max_alpha_square'
   *         in a simplicial complex data structure.
   *  @param[out] complex Simplicial complex data structure compatible which is a model of
   *              SimplicialComplexForWitness concept.
   *  @param[in] max_alpha_square Maximal squared relaxation parameter.
   *  @param[in] limit_dimension Represents the maximal dimension of the simplicial complex
   *         (default value = no limit).
   */
  template < typename SimplicialComplexForWitness >
  bool create_complex(SimplicialComplexForWitness& complex,
                      FT  max_alpha_square,
                      Landmark_id limit_dimension = std::numeric_limits<Landmark_id>::max())       
  {
    std::size_t nbL = landmarks_.size();
    if (complex.num_vertices() > 0) {
      std::cerr << "Witness complex cannot create complex - complex is not empty.\n";
      return false;
    }
    if (max_alpha_square < 0) {
      std::cerr << "Witness complex cannot create complex - squared relaxation parameter must be non-negative.\n";
      return false;
    }
    if (limit_dimension < 0) {
      std::cerr << "Witness complex cannot create complex - limit dimension must be non-negative.\n";
      return false;
    }
    typeVectorVertex vv;
    ActiveWitnessList active_witnesses;
    for (unsigned i = 0; i != nbL; ++i) {
      // initial fill of 0-dimensional simplices
      // by doing it we don't assume that landmarks are necessarily witnesses themselves anymore
      vv = {i};
      complex.insert_simplex(vv, Filtration_value(0.0));
    }
    Landmark_id k = 1; /* current dimension in iterative construction */
    for (auto w: witnesses_)
      active_witnesses.push_back(ActiveWitness(landmark_tree_.query_incremental_nearest_neighbors(w)));
    ActiveWitness aw_copy(active_witnesses.front());
    while (!active_witnesses.empty() && k <= limit_dimension ) {
      typename ActiveWitnessList::iterator aw_it = active_witnesses.begin();
      std::vector<Landmark_id> simplex;
      simplex.reserve(k+1);
      while (aw_it != active_witnesses.end()) {
        bool ok = add_all_faces_of_dimension(k,
                                             max_alpha_square,
                                             std::numeric_limits<double>::infinity(),
                                             aw_it->begin(),
                                             simplex,
                                             complex,
                                             aw_it->end());
        assert(simplex.empty());
        if (!ok)
          active_witnesses.erase(aw_it++); //First increase the iterator and then erase the previous element
        else
          aw_it++;
      } 
      k++;
    }
    complex.set_dimension(k-1);
    return true;
  }

  //@}

 private:
  /* \brief Adds recursively all the faces of a certain dimension dim witnessed by the same witness
   * Iterator is needed to know until how far we can take landmarks to form simplexes
   * simplex is the prefix of the simplexes to insert
   * The output value indicates if the witness rests active or not
   */
  template < typename SimplicialComplexForWitness >
  bool add_all_faces_of_dimension(int dim,
                                  double alpha2,
                                  double norelax_dist2,
                                  typename ActiveWitness::iterator curr_l,
                                  std::vector<Landmark_id>& simplex,
                                  SimplicialComplexForWitness& sc,
                                  typename ActiveWitness::iterator end)
  {
    if (curr_l == end)
      return false;
    bool will_be_active = false;
    typename ActiveWitness::iterator l_it = curr_l;
    if (dim > 0)
      for (; l_it->second - alpha2 <= norelax_dist2 && l_it != end; ++l_it) {
        simplex.push_back(l_it->first);
        if (sc.find(simplex) != sc.null_simplex()) {
          typename ActiveWitness::iterator next_it = l_it;
          will_be_active = add_all_faces_of_dimension(dim-1,
                                                      alpha2,
                                                      norelax_dist2,
                                                      ++next_it,
                                                      simplex,
                                                      sc,
                                                      end) || will_be_active;
        }
        assert(!simplex.empty());
        simplex.pop_back();
        // If norelax_dist is infinity, change to first omitted distance
        if (l_it->second <= norelax_dist2)
          norelax_dist2 = l_it->second;
        typename ActiveWitness::iterator next_it = l_it;
        will_be_active = add_all_faces_of_dimension(dim,
                                                    alpha2,
                                                    norelax_dist2,
                                                    ++next_it,
                                                    simplex,
                                                    sc,
                                                    end) || will_be_active;
      } 
    else if (dim == 0)
      for (; l_it->second - alpha2 <= norelax_dist2 && l_it != end; ++l_it) {
        simplex.push_back(l_it->first);
        double filtration_value = 0;
        // if norelax_dist is infinite, relaxation is 0.
        if (l_it->second > norelax_dist2) 
          filtration_value = l_it->second - norelax_dist2;
        if (all_faces_in(simplex, &filtration_value, sc)) {
          will_be_active = true;
          sc.insert_simplex(simplex, filtration_value);
        }
        assert(!simplex.empty());
        simplex.pop_back();
        // If norelax_dist is infinity, change to first omitted distance
        if (l_it->second < norelax_dist2)
          norelax_dist2 = l_it->second;
      } 
    return will_be_active;
  }
  
  /* \brief Check if the facets of the k-dimensional simplex witnessed 
   *  by witness witness_id are already in the complex.
   *  inserted_vertex is the handle of the (k+1)-th vertex witnessed by witness_id
   */
  template < typename SimplicialComplexForWitness >
  bool all_faces_in(typeVectorVertex& simplex,
                    double* filtration_value,
                    SimplicialComplexForWitness& sc)
  {
    typedef typename SimplicialComplexForWitness::Simplex_handle Simplex_handle;

    typeVectorVertex facet;
    for (typename typeVectorVertex::iterator not_it = simplex.begin(); not_it != simplex.end(); ++not_it)
      {
        facet.clear();
        for (typename typeVectorVertex::iterator it = simplex.begin(); it != simplex.end(); ++it)
          if (it != not_it)
            facet.push_back(*it);
        Simplex_handle facet_sh = sc.find(facet);
        if (facet_sh == sc.null_simplex())
          return false;
        else if (sc.filtration(facet_sh) > *filtration_value)
          *filtration_value = sc.filtration(facet_sh);
      }
    return true;
  }
};

}  // namespace witness_complex

}  // namespace Gudhi

#endif  // WITNESS_COMPLEX_H_

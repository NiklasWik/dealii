// ---------------------------------------------------------------------
//
// Copyright (C) 2003 - 2021 by the deal.II authors
//
// This file is part of the deal.II library.
//
// The deal.II library is free software; you can use it, redistribute
// it, and/or modify it under the terms of the GNU Lesser General
// Public License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// The full text of the license can be found in the file LICENSE.md at
// the top level directory of deal.II.
//
// ---------------------------------------------------------------------


#include <deal.II/base/polynomial.h>
#include <deal.II/base/qprojector.h>
#include <deal.II/base/quadrature_lib.h>
#include <deal.II/base/tensor_product_polynomials.h>

#include <deal.II/dofs/dof_accessor.h>

#include <deal.II/fe/fe.h>
#include <deal.II/fe/fe_nothing.h>
#include <deal.II/fe/fe_raviart_thomas.h>
#include <deal.II/fe/fe_tools.h>
#include <deal.II/fe/mapping.h>

#include <memory>
#include <sstream>


DEAL_II_NAMESPACE_OPEN


// ---------------- polynomial class for FE_RaviartThomasNodal ---------------

namespace
{
  template <int dim>
  class PolynomialsRaviartThomasNodal : public TensorPolynomialsBase<dim>
  {
  public:
    PolynomialsRaviartThomasNodal(const unsigned int degree);

    /**
     * Compute the value and derivatives of each Raviart-Thomas polynomial at
     * @p unit_point.
     *
     * The size of the vectors must either be zero or equal <tt>n()</tt>. In
     * the first case, the function will not compute these values.
     */
    void
    evaluate(const Point<dim> &           unit_point,
             std::vector<Tensor<1, dim>> &values,
             std::vector<Tensor<2, dim>> &grads,
             std::vector<Tensor<3, dim>> &grad_grads,
             std::vector<Tensor<4, dim>> &third_derivatives,
             std::vector<Tensor<5, dim>> &fourth_derivatives) const override;

    /**
     * Return the name of the space, which is <tt>PolynomialsRaviartThomas</tt>.
     */
    std::string
    name() const override;

    /**
     * Return the number of polynomials in the space without requiring to
     * build an object of PolynomialsRaviartThomas. This is required by the
     * FiniteElement classes.
     */
    static unsigned int
    n_polynomials(const unsigned int degree);

    const std::vector<unsigned int> &
    get_renumbering() const;

    /**
     * @copydoc TensorPolynomialsBase<dim>::clone()
     */
    virtual std::unique_ptr<TensorPolynomialsBase<dim>>
    clone() const override;

    /**
     * Compute the generalized support points of the associated element in the
     * ordering of the element. Note that they are not support points in the
     * classical sense as polynomials of the different components have
     * different points, which need to be combined in terms of Piola
     * transforms.
     */
    std::vector<Point<dim>>
    get_polynomial_support_points() const;

  private:
    /**
     * The degree variable passed to the constructor.
     */
    const unsigned int degree;

    /**
     * An object representing the polynomial space for a single component. We
     * can re-use it by rotating the coordinates of the evaluation point.
     */
    const AnisotropicPolynomials<dim> polynomial_space;

    /**
     * Renumbering from lexicographic to hierarchic order.
     */
    std::vector<unsigned int> lexicographic_to_hierarchic;

    /**
     * Renumbering from hierarchic to lexicographic order. Inverse of
     * lexicographic_to_hierarchic.
     */
    std::vector<unsigned int> hierarchic_to_lexicographic;

    /**
     * Renumbering from shifted polynomial spaces to lexicographic one
     */
    std::array<std::vector<unsigned int>, dim> renumber_aniso;
  };



  // Create nodal Raviart-Thomas polynomials as the tensor product of Lagrange
  // polynomials on Gauss-Lobatto points of degree + 2 points in the
  // continuous direction and degree + 1 points in the discontinuous
  // directions (we could also choose Lagrange polynomials on Gauss points but
  // those are slightly more expensive to handle in classes).
  std::vector<std::vector<Polynomials::Polynomial<double>>>
  create_rt_polynomials(const unsigned int dim, const unsigned int degree)
  {
    std::vector<std::vector<Polynomials::Polynomial<double>>> pols(dim);
    pols[0] = Polynomials::generate_complete_Lagrange_basis(
      QGaussLobatto<1>(degree + 2).get_points());
    if (degree > 0)
      for (unsigned int d = 1; d < dim; ++d)
        pols[d] = Polynomials::generate_complete_Lagrange_basis(
          QGaussLobatto<1>(degree + 1).get_points());
    else
      for (unsigned int d = 1; d < dim; ++d)
        pols[d] = Polynomials::generate_complete_Lagrange_basis(
          QMidpoint<1>().get_points());

    return pols;
  }


  template <int dim>
  PolynomialsRaviartThomasNodal<dim>::PolynomialsRaviartThomasNodal(
    const unsigned int degree)
    : TensorPolynomialsBase<dim>(degree, n_polynomials(degree))
    , degree(degree)
    , polynomial_space(create_rt_polynomials(dim, degree))
  {
    // create renumbering of the unknowns from the lexicographic order to the
    // actual order required by the finite element class with unknowns on
    // faces placed first
    const unsigned int n_pols = polynomial_space.n();
    lexicographic_to_hierarchic =
      internal::get_lexicographic_numbering_rt_nodal<dim>(degree + 1);

    hierarchic_to_lexicographic =
      Utilities::invert_permutation(lexicographic_to_hierarchic);

    // since we only store an anisotropic polynomial for the first component,
    // we set up a second numbering to switch out the actual coordinate
    // direction
    renumber_aniso[0].resize(n_pols);
    for (unsigned int i = 0; i < n_pols; ++i)
      renumber_aniso[0][i] = i;
    if (dim == 2)
      {
        // switch x and y component (i and j loops)
        renumber_aniso[1].resize(n_pols);
        for (unsigned int j = 0; j < degree + 2; ++j)
          for (unsigned int i = 0; i < degree + 1; ++i)
            renumber_aniso[1][j * (degree + 1) + i] = j + i * (degree + 2);
      }
    if (dim == 3)
      {
        // switch x, y, and z component (i, j, k) -> (j, k, i)
        renumber_aniso[1].resize(n_pols);
        for (unsigned int k = 0; k < degree + 1; ++k)
          for (unsigned int j = 0; j < degree + 2; ++j)
            for (unsigned int i = 0; i < degree + 1; ++i)
              renumber_aniso[1][(k * (degree + 2) + j) * (degree + 1) + i] =
                j + k * (degree + 2) + i * (degree + 2) * (degree + 1);

        // switch x, y, and z component (i, j, k) -> (k, i, j)
        renumber_aniso[2].resize(n_pols);
        for (unsigned int k = 0; k < degree + 2; ++k)
          for (unsigned int j = 0; j < degree + 1; ++j)
            for (unsigned int i = 0; i < degree + 1; ++i)
              renumber_aniso[2][(k * (degree + 1) + j) * (degree + 1) + i] =
                k + i * (degree + 2) + j * (degree + 2) * (degree + 1);
      }
  }



  template <int dim>
  void
  PolynomialsRaviartThomasNodal<dim>::evaluate(
    const Point<dim> &           unit_point,
    std::vector<Tensor<1, dim>> &values,
    std::vector<Tensor<2, dim>> &grads,
    std::vector<Tensor<3, dim>> &grad_grads,
    std::vector<Tensor<4, dim>> &third_derivatives,
    std::vector<Tensor<5, dim>> &fourth_derivatives) const
  {
    Assert(values.size() == this->n() || values.size() == 0,
           ExcDimensionMismatch(values.size(), this->n()));
    Assert(grads.size() == this->n() || grads.size() == 0,
           ExcDimensionMismatch(grads.size(), this->n()));
    Assert(grad_grads.size() == this->n() || grad_grads.size() == 0,
           ExcDimensionMismatch(grad_grads.size(), this->n()));
    Assert(third_derivatives.size() == this->n() ||
             third_derivatives.size() == 0,
           ExcDimensionMismatch(third_derivatives.size(), this->n()));
    Assert(fourth_derivatives.size() == this->n() ||
             fourth_derivatives.size() == 0,
           ExcDimensionMismatch(fourth_derivatives.size(), this->n()));

    std::vector<double>         p_values;
    std::vector<Tensor<1, dim>> p_grads;
    std::vector<Tensor<2, dim>> p_grad_grads;
    std::vector<Tensor<3, dim>> p_third_derivatives;
    std::vector<Tensor<4, dim>> p_fourth_derivatives;

    const unsigned int n_sub = polynomial_space.n();
    p_values.resize((values.size() == 0) ? 0 : n_sub);
    p_grads.resize((grads.size() == 0) ? 0 : n_sub);
    p_grad_grads.resize((grad_grads.size() == 0) ? 0 : n_sub);
    p_third_derivatives.resize((third_derivatives.size() == 0) ? 0 : n_sub);
    p_fourth_derivatives.resize((fourth_derivatives.size() == 0) ? 0 : n_sub);

    for (unsigned int d = 0; d < dim; ++d)
      {
        // First we copy the point. The polynomial space for component d
        // consists of polynomials of degree k in x_d and degree k+1 in the
        // other variables. in order to simplify this, we use the same
        // AnisotropicPolynomial space and simply rotate the coordinates
        // through all directions.
        Point<dim> p;
        for (unsigned int c = 0; c < dim; ++c)
          p(c) = unit_point((c + d) % dim);

        polynomial_space.evaluate(p,
                                  p_values,
                                  p_grads,
                                  p_grad_grads,
                                  p_third_derivatives,
                                  p_fourth_derivatives);

        for (unsigned int i = 0; i < p_values.size(); ++i)
          values[lexicographic_to_hierarchic[i + d * n_sub]][d] =
            p_values[renumber_aniso[d][i]];

        for (unsigned int i = 0; i < p_grads.size(); ++i)
          for (unsigned int d1 = 0; d1 < dim; ++d1)
            grads[lexicographic_to_hierarchic[i + d * n_sub]][d]
                 [(d1 + d) % dim] = p_grads[renumber_aniso[d][i]][d1];

        for (unsigned int i = 0; i < p_grad_grads.size(); ++i)
          for (unsigned int d1 = 0; d1 < dim; ++d1)
            for (unsigned int d2 = 0; d2 < dim; ++d2)
              grad_grads[lexicographic_to_hierarchic[i + d * n_sub]][d]
                        [(d1 + d) % dim][(d2 + d) % dim] =
                          p_grad_grads[renumber_aniso[d][i]][d1][d2];

        for (unsigned int i = 0; i < p_third_derivatives.size(); ++i)
          for (unsigned int d1 = 0; d1 < dim; ++d1)
            for (unsigned int d2 = 0; d2 < dim; ++d2)
              for (unsigned int d3 = 0; d3 < dim; ++d3)
                third_derivatives[lexicographic_to_hierarchic[i + d * n_sub]][d]
                                 [(d1 + d) % dim][(d2 + d) % dim]
                                 [(d3 + d) % dim] =
                                   p_third_derivatives[renumber_aniso[d][i]][d1]
                                                      [d2][d3];

        for (unsigned int i = 0; i < p_fourth_derivatives.size(); ++i)
          for (unsigned int d1 = 0; d1 < dim; ++d1)
            for (unsigned int d2 = 0; d2 < dim; ++d2)
              for (unsigned int d3 = 0; d3 < dim; ++d3)
                for (unsigned int d4 = 0; d4 < dim; ++d4)
                  fourth_derivatives[lexicographic_to_hierarchic[i + d * n_sub]]
                                    [d][(d1 + d) % dim][(d2 + d) % dim]
                                    [(d3 + d) % dim][(d4 + d) % dim] =
                                      p_fourth_derivatives[renumber_aniso[d][i]]
                                                          [d1][d2][d3][d4];
      }
  }



  template <int dim>
  std::string
  PolynomialsRaviartThomasNodal<dim>::name() const
  {
    return "PolynomialsRaviartThomasNodal";
  }



  template <int dim>
  unsigned int
  PolynomialsRaviartThomasNodal<dim>::n_polynomials(unsigned int degree)
  {
    return dim * (degree + 2) * Utilities::pow(degree + 1, dim - 1);
  }



  template <int dim>
  std::unique_ptr<TensorPolynomialsBase<dim>>
  PolynomialsRaviartThomasNodal<dim>::clone() const
  {
    return std::make_unique<PolynomialsRaviartThomasNodal<dim>>(*this);
  }



  template <int dim>
  std::vector<Point<dim>>
  PolynomialsRaviartThomasNodal<dim>::get_polynomial_support_points() const
  {
    Assert(dim > 0 && dim <= 3, ExcImpossibleInDim(dim));
    const Quadrature<1> low(
      degree == 0 ? static_cast<Quadrature<1>>(QMidpoint<1>()) :
                    static_cast<Quadrature<1>>(QGaussLobatto<1>(degree + 1)));
    const QGaussLobatto<1>  high(degree + 2);
    const QAnisotropic<dim> quad =
      (dim == 1 ? QAnisotropic<dim>(high) :
                  (dim == 2 ? QAnisotropic<dim>(high, low) :
                              QAnisotropic<dim>(high, low, low)));

    const unsigned int      n_sub = polynomial_space.n();
    std::vector<Point<dim>> points(dim * n_sub);
    points.resize(n_polynomials(degree));
    for (unsigned int d = 0; d < dim; ++d)
      for (unsigned int i = 0; i < n_sub; ++i)
        points[lexicographic_to_hierarchic[i + d * n_sub]] =
          quad.point(renumber_aniso[d][i]);
    return points;
  }



  // Return a vector of "dofs per object" where the components of the returned
  // vector refer to:
  // 0 = vertex
  // 1 = edge
  // 2 = face (which is a cell in 2D)
  // 3 = cell
  std::vector<unsigned int>
  get_rt_dpo_vector(const unsigned int dim, const unsigned int degree)
  {
    std::vector<unsigned int> dpo(dim + 1);
    dpo[0]                     = 0;
    dpo[1]                     = 0;
    unsigned int dofs_per_face = 1;
    for (unsigned int d = 1; d < dim; ++d)
      dofs_per_face *= (degree + 1);

    dpo[dim - 1] = dofs_per_face;
    dpo[dim]     = dim * degree * dofs_per_face;

    return dpo;
  }
} // namespace



// --------------------- actual implementation of element --------------------

template <int dim>
FE_RaviartThomasNodal<dim>::FE_RaviartThomasNodal(const unsigned int degree)
  : FE_PolyTensor<dim>(PolynomialsRaviartThomasNodal<dim>(degree),
                       FiniteElementData<dim>(get_rt_dpo_vector(dim, degree),
                                              dim,
                                              degree + 1,
                                              FiniteElementData<dim>::Hdiv),
                       std::vector<bool>(1, false),
                       std::vector<ComponentMask>(
                         PolynomialsRaviartThomasNodal<dim>::n_polynomials(
                           degree),
                         std::vector<bool>(dim, true)))
{
  Assert(dim >= 2, ExcImpossibleInDim(dim));

  this->mapping_kind = {mapping_raviart_thomas};

  // First, initialize the generalized support points and quadrature weights,
  // since they are required for interpolation.
  this->generalized_support_points =
    PolynomialsRaviartThomasNodal<dim>(degree).get_polynomial_support_points();
  AssertDimension(this->generalized_support_points.size(),
                  this->n_dofs_per_cell());

  const unsigned int face_no = 0;
  if (dim > 1)
    this->generalized_face_support_points[face_no] =
      degree == 0 ? QGauss<dim - 1>(1).get_points() :
                    QGaussLobatto<dim - 1>(degree + 1).get_points();

  FullMatrix<double> face_embeddings[GeometryInfo<dim>::max_children_per_face];
  for (unsigned int i = 0; i < GeometryInfo<dim>::max_children_per_face; ++i)
    face_embeddings[i].reinit(this->n_dofs_per_face(face_no),
                              this->n_dofs_per_face(face_no));
  FETools::compute_face_embedding_matrices<dim, double>(*this,
                                                        face_embeddings,
                                                        0,
                                                        0);
  this->interface_constraints.reinit(GeometryInfo<dim>::max_children_per_face *
                                       this->n_dofs_per_face(face_no),
                                     this->n_dofs_per_face(face_no));
  unsigned int target_row = 0;
  for (unsigned int d = 0; d < GeometryInfo<dim>::max_children_per_face; ++d)
    for (unsigned int i = 0; i < face_embeddings[d].m(); ++i)
      {
        for (unsigned int j = 0; j < face_embeddings[d].n(); ++j)
          this->interface_constraints(target_row, j) = face_embeddings[d](i, j);
        ++target_row;
      }

  // We need to initialize the dof permutation table and the one for the sign
  // change.
  initialize_quad_dof_index_permutation_and_sign_change();
}



template <int dim>
std::string
FE_RaviartThomasNodal<dim>::get_name() const
{
  // note that the FETools::get_fe_by_name function depends on the particular
  // format of the string this function returns, so they have to be kept in
  // synch

  // note that this->degree is the maximal polynomial degree and is thus one
  // higher than the argument given to the constructor
  return "FE_RaviartThomasNodal<" + std::to_string(dim) + ">(" +
         std::to_string(this->degree - 1) + ")";
}


template <int dim>
std::unique_ptr<FiniteElement<dim, dim>>
FE_RaviartThomasNodal<dim>::clone() const
{
  return std::make_unique<FE_RaviartThomasNodal<dim>>(*this);
}


//---------------------------------------------------------------------------
// Auxiliary and internal functions
//---------------------------------------------------------------------------



template <int dim>
void
FE_RaviartThomasNodal<
  dim>::initialize_quad_dof_index_permutation_and_sign_change()
{
  // for 1D and 2D, do nothing
  if (dim < 3)
    return;

  const unsigned int n       = this->degree;
  const unsigned int face_no = 0;
  Assert(n * n == this->n_dofs_per_quad(face_no), ExcInternalError());
  for (unsigned int local = 0; local < this->n_dofs_per_quad(face_no); ++local)
    // face support points are in lexicographic ordering with x running
    // fastest. invert that (y running fastest)
    {
      unsigned int i = local % n, j = local / n;

      // face_orientation=false, face_flip=false, face_rotation=false
      this->adjust_quad_dof_index_for_face_orientation_table[face_no](local,
                                                                      0) =
        j + i * n - local;
      // face_orientation=false, face_flip=false, face_rotation=true
      this->adjust_quad_dof_index_for_face_orientation_table[face_no](local,
                                                                      1) =
        i + (n - 1 - j) * n - local;
      // face_orientation=false, face_flip=true,  face_rotation=false
      this->adjust_quad_dof_index_for_face_orientation_table[face_no](local,
                                                                      2) =
        (n - 1 - j) + (n - 1 - i) * n - local;
      // face_orientation=false, face_flip=true,  face_rotation=true
      this->adjust_quad_dof_index_for_face_orientation_table[face_no](local,
                                                                      3) =
        (n - 1 - i) + j * n - local;
      // face_orientation=true,  face_flip=false, face_rotation=false
      this->adjust_quad_dof_index_for_face_orientation_table[face_no](local,
                                                                      4) = 0;
      // face_orientation=true,  face_flip=false, face_rotation=true
      this->adjust_quad_dof_index_for_face_orientation_table[face_no](local,
                                                                      5) =
        j + (n - 1 - i) * n - local;
      // face_orientation=true,  face_flip=true,  face_rotation=false
      this->adjust_quad_dof_index_for_face_orientation_table[face_no](local,
                                                                      6) =
        (n - 1 - i) + (n - 1 - j) * n - local;
      // face_orientation=true,  face_flip=true,  face_rotation=true
      this->adjust_quad_dof_index_for_face_orientation_table[face_no](local,
                                                                      7) =
        (n - 1 - j) + i * n - local;

      // for face_orientation == false, we need to switch the sign
      for (unsigned int i = 0; i < 4; ++i)
        this->adjust_quad_dof_sign_for_face_orientation_table[face_no](local,
                                                                       i) = 1;
    }
}



template <int dim>
bool
FE_RaviartThomasNodal<dim>::has_support_on_face(
  const unsigned int shape_index,
  const unsigned int face_index) const
{
  AssertIndexRange(shape_index, this->n_dofs_per_cell());
  AssertIndexRange(face_index, GeometryInfo<dim>::faces_per_cell);

  // The first degrees of freedom are on the faces and each face has degree
  // degrees.
  const unsigned int support_face = shape_index / this->n_dofs_per_face();

  // The only thing we know for sure is that shape functions with support on
  // one face are zero on the opposite face.
  if (support_face < GeometryInfo<dim>::faces_per_cell)
    return (face_index != GeometryInfo<dim>::opposite_face[support_face]);

  // In all other cases, return true, which is safe
  return true;
}



template <int dim>
void
FE_RaviartThomasNodal<dim>::
  convert_generalized_support_point_values_to_dof_values(
    const std::vector<Vector<double>> &support_point_values,
    std::vector<double> &              nodal_values) const
{
  Assert(support_point_values.size() == this->generalized_support_points.size(),
         ExcDimensionMismatch(support_point_values.size(),
                              this->generalized_support_points.size()));
  Assert(nodal_values.size() == this->n_dofs_per_cell(),
         ExcDimensionMismatch(nodal_values.size(), this->n_dofs_per_cell()));
  Assert(support_point_values[0].size() == this->n_components(),
         ExcDimensionMismatch(support_point_values[0].size(),
                              this->n_components()));

  // First do interpolation on faces. There, the component evaluated depends
  // on the face direction and orientation.
  unsigned int fbase = 0;
  unsigned int f     = 0;
  for (; f < GeometryInfo<dim>::faces_per_cell;
       ++f, fbase += this->n_dofs_per_face(f))
    {
      for (unsigned int i = 0; i < this->n_dofs_per_face(f); ++i)
        {
          nodal_values[fbase + i] = support_point_values[fbase + i](
            GeometryInfo<dim>::unit_normal_direction[f]);
        }
    }

  // The remaining points form dim chunks, one for each component
  const unsigned int istep = (this->n_dofs_per_cell() - fbase) / dim;
  Assert((this->n_dofs_per_cell() - fbase) % dim == 0, ExcInternalError());

  f = 0;
  while (fbase < this->n_dofs_per_cell())
    {
      for (unsigned int i = 0; i < istep; ++i)
        {
          nodal_values[fbase + i] = support_point_values[fbase + i](f);
        }
      fbase += istep;
      ++f;
    }
  Assert(fbase == this->n_dofs_per_cell(), ExcInternalError());
}



// TODO: There are tests that check that the following few functions don't
// produce assertion failures, but none that actually check whether they do the
// right thing. one example for such a test would be to project a function onto
// an hp-space and make sure that the convergence order is correct with regard
// to the lowest used polynomial degree

template <int dim>
bool
FE_RaviartThomasNodal<dim>::hp_constraints_are_implemented() const
{
  return true;
}


template <int dim>
std::vector<std::pair<unsigned int, unsigned int>>
FE_RaviartThomasNodal<dim>::hp_vertex_dof_identities(
  const FiniteElement<dim> &fe_other) const
{
  // we can presently only compute these identities if both FEs are
  // FE_RaviartThomasNodals or the other is FE_Nothing.  In either case, no
  // dofs are assigned on the vertex, so we shouldn't be getting here at all.
  if (dynamic_cast<const FE_RaviartThomasNodal<dim> *>(&fe_other) != nullptr)
    return std::vector<std::pair<unsigned int, unsigned int>>();
  else if (dynamic_cast<const FE_Nothing<dim> *>(&fe_other) != nullptr)
    return std::vector<std::pair<unsigned int, unsigned int>>();
  else
    {
      Assert(false, ExcNotImplemented());
      return std::vector<std::pair<unsigned int, unsigned int>>();
    }
}



template <int dim>
std::vector<std::pair<unsigned int, unsigned int>>
FE_RaviartThomasNodal<dim>::hp_line_dof_identities(
  const FiniteElement<dim> &fe_other) const
{
  // we can presently only compute these identities if both FEs are
  // FE_RaviartThomasNodals or if the other one is FE_Nothing
  if (const FE_RaviartThomasNodal<dim> *fe_q_other =
        dynamic_cast<const FE_RaviartThomasNodal<dim> *>(&fe_other))
    {
      // dofs are located on faces; these are only lines in 2d
      if (dim != 2)
        return std::vector<std::pair<unsigned int, unsigned int>>();

      // dofs are located along lines, so two dofs are identical only if in
      // the following two cases (remember that the face support points are
      // Gauss points):
      // 1. this->degree = fe_q_other->degree,
      //   in the case, all the dofs on the line are identical
      // 2. this->degree-1 and fe_q_other->degree-1
      //   are both even, i.e. this->dof_per_line and fe_q_other->dof_per_line
      //   are both odd, there exists only one point (the middle one) such
      //   that dofs are identical on this point
      //
      // to understand this, note that this->degree is the *maximal*
      // polynomial degree, and is thus one higher than the argument given to
      // the constructor
      const unsigned int p = this->degree - 1;
      const unsigned int q = fe_q_other->degree - 1;

      std::vector<std::pair<unsigned int, unsigned int>> identities;

      if (p == q)
        for (unsigned int i = 0; i < p + 1; ++i)
          identities.emplace_back(i, i);

      else if (p % 2 == 0 && q % 2 == 0)
        identities.emplace_back(p / 2, q / 2);

      return identities;
    }
  else if (dynamic_cast<const FE_Nothing<dim> *>(&fe_other) != nullptr)
    {
      // the FE_Nothing has no degrees of freedom, so there are no
      // equivalencies to be recorded
      return std::vector<std::pair<unsigned int, unsigned int>>();
    }
  else
    {
      Assert(false, ExcNotImplemented());
      return std::vector<std::pair<unsigned int, unsigned int>>();
    }
}


template <int dim>
std::vector<std::pair<unsigned int, unsigned int>>
FE_RaviartThomasNodal<dim>::hp_quad_dof_identities(
  const FiniteElement<dim> &fe_other,
  const unsigned int        face_no) const
{
  // we can presently only compute these identities if both FEs are
  // FE_RaviartThomasNodals or if the other one is FE_Nothing
  if (const FE_RaviartThomasNodal<dim> *fe_q_other =
        dynamic_cast<const FE_RaviartThomasNodal<dim> *>(&fe_other))
    {
      // dofs are located on faces; these are only quads in 3d
      if (dim != 3)
        return std::vector<std::pair<unsigned int, unsigned int>>();

      // this works exactly like the line case above
      const unsigned int p = this->n_dofs_per_quad(face_no);

      AssertDimension(fe_q_other->n_unique_faces(), 1);
      const unsigned int q = fe_q_other->n_dofs_per_quad(0);

      std::vector<std::pair<unsigned int, unsigned int>> identities;

      if (p == q)
        for (unsigned int i = 0; i < p; ++i)
          identities.emplace_back(i, i);

      else if (p % 2 != 0 && q % 2 != 0)
        identities.emplace_back(p / 2, q / 2);

      return identities;
    }
  else if (dynamic_cast<const FE_Nothing<dim> *>(&fe_other) != nullptr)
    {
      // the FE_Nothing has no degrees of freedom, so there are no
      // equivalencies to be recorded
      return std::vector<std::pair<unsigned int, unsigned int>>();
    }
  else
    {
      Assert(false, ExcNotImplemented());
      return std::vector<std::pair<unsigned int, unsigned int>>();
    }
}


template <int dim>
FiniteElementDomination::Domination
FE_RaviartThomasNodal<dim>::compare_for_domination(
  const FiniteElement<dim> &fe_other,
  const unsigned int        codim) const
{
  Assert(codim <= dim, ExcImpossibleInDim(dim));
  (void)codim;

  // vertex/line/face/cell domination
  // --------------------------------
  if (const FE_RaviartThomasNodal<dim> *fe_rt_nodal_other =
        dynamic_cast<const FE_RaviartThomasNodal<dim> *>(&fe_other))
    {
      if (this->degree < fe_rt_nodal_other->degree)
        return FiniteElementDomination::this_element_dominates;
      else if (this->degree == fe_rt_nodal_other->degree)
        return FiniteElementDomination::either_element_can_dominate;
      else
        return FiniteElementDomination::other_element_dominates;
    }
  else if (const FE_Nothing<dim> *fe_nothing =
             dynamic_cast<const FE_Nothing<dim> *>(&fe_other))
    {
      if (fe_nothing->is_dominating())
        return FiniteElementDomination::other_element_dominates;
      else
        // the FE_Nothing has no degrees of freedom and it is typically used
        // in a context where we don't require any continuity along the
        // interface
        return FiniteElementDomination::no_requirements;
    }

  Assert(false, ExcNotImplemented());
  return FiniteElementDomination::neither_element_dominates;
}



template <>
void
FE_RaviartThomasNodal<1>::get_face_interpolation_matrix(
  const FiniteElement<1, 1> & /*x_source_fe*/,
  FullMatrix<double> & /*interpolation_matrix*/,
  const unsigned int) const
{
  Assert(false, ExcImpossibleInDim(1));
}


template <>
void
FE_RaviartThomasNodal<1>::get_subface_interpolation_matrix(
  const FiniteElement<1, 1> & /*x_source_fe*/,
  const unsigned int /*subface*/,
  FullMatrix<double> & /*interpolation_matrix*/,
  const unsigned int) const
{
  Assert(false, ExcImpossibleInDim(1));
}



template <int dim>
void
FE_RaviartThomasNodal<dim>::get_face_interpolation_matrix(
  const FiniteElement<dim> &x_source_fe,
  FullMatrix<double> &      interpolation_matrix,
  const unsigned int        face_no) const
{
  // this is only implemented, if the
  // source FE is also a
  // RaviartThomasNodal element
  AssertThrow((x_source_fe.get_name().find("FE_RaviartThomasNodal<") == 0) ||
                (dynamic_cast<const FE_RaviartThomasNodal<dim> *>(
                   &x_source_fe) != nullptr),
              typename FiniteElement<dim>::ExcInterpolationNotImplemented());

  Assert(interpolation_matrix.n() == this->n_dofs_per_face(face_no),
         ExcDimensionMismatch(interpolation_matrix.n(),
                              this->n_dofs_per_face(face_no)));
  Assert(interpolation_matrix.m() == x_source_fe.n_dofs_per_face(face_no),
         ExcDimensionMismatch(interpolation_matrix.m(),
                              x_source_fe.n_dofs_per_face(face_no)));

  // ok, source is a RaviartThomasNodal element, so we will be able to do the
  // work
  const FE_RaviartThomasNodal<dim> &source_fe =
    dynamic_cast<const FE_RaviartThomasNodal<dim> &>(x_source_fe);

  // Make sure that the element for which the DoFs should be constrained is
  // the one with the higher polynomial degree.  Actually the procedure will
  // work also if this assertion is not satisfied. But the matrices produced
  // in that case might lead to problems in the hp-procedures, which use this
  // method.
  Assert(this->n_dofs_per_face(face_no) <= source_fe.n_dofs_per_face(face_no),
         typename FiniteElement<dim>::ExcInterpolationNotImplemented());

  // generate a quadrature with the generalized support points.  This is later
  // based as a basis for the QProjector, which returns the support points on
  // the face.
  Quadrature<dim - 1> quad_face_support(
    source_fe.generalized_face_support_points[face_no]);

  // Rule of thumb for FP accuracy, that can be expected for a given
  // polynomial degree.  This value is used to cut off values close to zero.
  double eps = 2e-13 * this->degree * (dim - 1);

  // compute the interpolation matrix by simply taking the value at the
  // support points.
  const Quadrature<dim> face_projection =
    QProjector<dim>::project_to_face(this->reference_cell(),
                                     quad_face_support,
                                     0);

  for (unsigned int i = 0; i < source_fe.n_dofs_per_face(face_no); ++i)
    {
      const Point<dim> &p = face_projection.point(i);

      for (unsigned int j = 0; j < this->n_dofs_per_face(face_no); ++j)
        {
          double matrix_entry =
            this->shape_value_component(this->face_to_cell_index(j, 0), p, 0);

          // Correct the interpolated value. I.e. if it is close to 1 or 0,
          // make it exactly 1 or 0. Unfortunately, this is required to avoid
          // problems with higher order elements.
          if (std::fabs(matrix_entry - 1.0) < eps)
            matrix_entry = 1.0;
          if (std::fabs(matrix_entry) < eps)
            matrix_entry = 0.0;

          interpolation_matrix(i, j) = matrix_entry;
        }
    }

#ifdef DEBUG
  // make sure that the row sum of each of the matrices is 1 at this
  // point. this must be so since the shape functions sum up to 1
  for (unsigned int j = 0; j < source_fe.n_dofs_per_face(face_no); ++j)
    {
      double sum = 0.;

      for (unsigned int i = 0; i < this->n_dofs_per_face(face_no); ++i)
        sum += interpolation_matrix(j, i);

      Assert(std::fabs(sum - 1) < 2e-13 * this->degree * (dim - 1),
             ExcInternalError());
    }
#endif
}


template <int dim>
void
FE_RaviartThomasNodal<dim>::get_subface_interpolation_matrix(
  const FiniteElement<dim> &x_source_fe,
  const unsigned int        subface,
  FullMatrix<double> &      interpolation_matrix,
  const unsigned int        face_no) const
{
  // this is only implemented, if the source FE is also a RaviartThomasNodal
  // element
  AssertThrow((x_source_fe.get_name().find("FE_RaviartThomasNodal<") == 0) ||
                (dynamic_cast<const FE_RaviartThomasNodal<dim> *>(
                   &x_source_fe) != nullptr),
              typename FiniteElement<dim>::ExcInterpolationNotImplemented());

  Assert(interpolation_matrix.n() == this->n_dofs_per_face(face_no),
         ExcDimensionMismatch(interpolation_matrix.n(),
                              this->n_dofs_per_face(face_no)));
  Assert(interpolation_matrix.m() == x_source_fe.n_dofs_per_face(face_no),
         ExcDimensionMismatch(interpolation_matrix.m(),
                              x_source_fe.n_dofs_per_face(face_no)));

  // ok, source is a RaviartThomasNodal element, so we will be able to do the
  // work
  const FE_RaviartThomasNodal<dim> &source_fe =
    dynamic_cast<const FE_RaviartThomasNodal<dim> &>(x_source_fe);

  // Make sure that the element for which the DoFs should be constrained is
  // the one with the higher polynomial degree. Actually the procedure will
  // work also if this assertion is not satisfied. But the matrices produced
  // in that case might lead to problems in the hp-procedures, which use this
  // method.
  Assert(this->n_dofs_per_face(face_no) <= source_fe.n_dofs_per_face(face_no),
         typename FiniteElement<dim>::ExcInterpolationNotImplemented());

  // generate a quadrature with the generalized support points.  This is later
  // based as a basis for the QProjector, which returns the support points on
  // the face.
  Quadrature<dim - 1> quad_face_support(
    source_fe.generalized_face_support_points[face_no]);

  // Rule of thumb for FP accuracy, that can be expected for a given
  // polynomial degree. This value is used to cut off values close to zero.
  double eps = 2e-13 * this->degree * (dim - 1);

  // compute the interpolation matrix by simply taking the value at the
  // support points.
  const Quadrature<dim> subface_projection =
    QProjector<dim>::project_to_subface(this->reference_cell(),
                                        quad_face_support,
                                        0,
                                        subface);

  for (unsigned int i = 0; i < source_fe.n_dofs_per_face(face_no); ++i)
    {
      const Point<dim> &p = subface_projection.point(i);

      for (unsigned int j = 0; j < this->n_dofs_per_face(face_no); ++j)
        {
          double matrix_entry =
            this->shape_value_component(this->face_to_cell_index(j, 0), p, 0);

          // Correct the interpolated value. I.e. if it is close to 1 or 0,
          // make it exactly 1 or 0. Unfortunately, this is required to avoid
          // problems with higher order elements.
          if (std::fabs(matrix_entry - 1.0) < eps)
            matrix_entry = 1.0;
          if (std::fabs(matrix_entry) < eps)
            matrix_entry = 0.0;

          interpolation_matrix(i, j) = matrix_entry;
        }
    }

#ifdef DEBUG
  // make sure that the row sum of each of the matrices is 1 at this
  // point. this must be so since the shape functions sum up to 1
  for (unsigned int j = 0; j < source_fe.n_dofs_per_face(face_no); ++j)
    {
      double sum = 0.;

      for (unsigned int i = 0; i < this->n_dofs_per_face(face_no); ++i)
        sum += interpolation_matrix(j, i);

      Assert(std::fabs(sum - 1) < 2e-13 * this->degree * (dim - 1),
             ExcInternalError());
    }
#endif
}



template <int dim>
const FullMatrix<double> &
FE_RaviartThomasNodal<dim>::get_prolongation_matrix(
  const unsigned int         child,
  const RefinementCase<dim> &refinement_case) const
{
  AssertIndexRange(refinement_case,
                   RefinementCase<dim>::isotropic_refinement + 1);
  Assert(refinement_case != RefinementCase<dim>::no_refinement,
         ExcMessage(
           "Prolongation matrices are only available for refined cells!"));
  AssertIndexRange(child, GeometryInfo<dim>::n_children(refinement_case));

  // initialization upon first request
  if (this->prolongation[refinement_case - 1][child].n() == 0)
    {
      std::lock_guard<std::mutex> lock(this->mutex);

      // if matrix got updated while waiting for the lock
      if (this->prolongation[refinement_case - 1][child].n() ==
          this->n_dofs_per_cell())
        return this->prolongation[refinement_case - 1][child];

      // now do the work. need to get a non-const version of data in order to
      // be able to modify them inside a const function
      FE_RaviartThomasNodal<dim> &this_nonconst =
        const_cast<FE_RaviartThomasNodal<dim> &>(*this);
      if (refinement_case == RefinementCase<dim>::isotropic_refinement)
        {
          std::vector<std::vector<FullMatrix<double>>> isotropic_matrices(
            RefinementCase<dim>::isotropic_refinement);
          isotropic_matrices.back().resize(
            GeometryInfo<dim>::n_children(RefinementCase<dim>(refinement_case)),
            FullMatrix<double>(this->n_dofs_per_cell(),
                               this->n_dofs_per_cell()));
          FETools::compute_embedding_matrices(*this, isotropic_matrices, true);
          this_nonconst.prolongation[refinement_case - 1].swap(
            isotropic_matrices.back());
        }
      else
        {
          // must compute both restriction and prolongation matrices because
          // we only check for their size and the reinit call initializes them
          // all
          this_nonconst.reinit_restriction_and_prolongation_matrices();
          FETools::compute_embedding_matrices(*this,
                                              this_nonconst.prolongation);
          FETools::compute_projection_matrices(*this,
                                               this_nonconst.restriction);
        }
    }

  // finally return the matrix
  return this->prolongation[refinement_case - 1][child];
}



template <int dim>
const FullMatrix<double> &
FE_RaviartThomasNodal<dim>::get_restriction_matrix(
  const unsigned int         child,
  const RefinementCase<dim> &refinement_case) const
{
  AssertIndexRange(refinement_case,
                   RefinementCase<dim>::isotropic_refinement + 1);
  Assert(refinement_case != RefinementCase<dim>::no_refinement,
         ExcMessage(
           "Restriction matrices are only available for refined cells!"));
  AssertIndexRange(child, GeometryInfo<dim>::n_children(refinement_case));

  // initialization upon first request
  if (this->restriction[refinement_case - 1][child].n() == 0)
    {
      std::lock_guard<std::mutex> lock(this->mutex);

      // if matrix got updated while waiting for the lock...
      if (this->restriction[refinement_case - 1][child].n() ==
          this->n_dofs_per_cell())
        return this->restriction[refinement_case - 1][child];

      // now do the work. need to get a non-const version of data in order to
      // be able to modify them inside a const function
      FE_RaviartThomasNodal<dim> &this_nonconst =
        const_cast<FE_RaviartThomasNodal<dim> &>(*this);
      if (refinement_case == RefinementCase<dim>::isotropic_refinement)
        {
          std::vector<std::vector<FullMatrix<double>>> isotropic_matrices(
            RefinementCase<dim>::isotropic_refinement);
          isotropic_matrices.back().resize(
            GeometryInfo<dim>::n_children(RefinementCase<dim>(refinement_case)),
            FullMatrix<double>(this->n_dofs_per_cell(),
                               this->n_dofs_per_cell()));
          FETools::compute_projection_matrices(*this, isotropic_matrices, true);
          this_nonconst.restriction[refinement_case - 1].swap(
            isotropic_matrices.back());
        }
      else
        {
          // must compute both restriction and prolongation matrices because
          // we only check for their size and the reinit call initializes them
          // all
          this_nonconst.reinit_restriction_and_prolongation_matrices();
          FETools::compute_embedding_matrices(*this,
                                              this_nonconst.prolongation);
          FETools::compute_projection_matrices(*this,
                                               this_nonconst.restriction);
        }
    }

  // finally return the matrix
  return this->restriction[refinement_case - 1][child];
}



// explicit instantiations
#include "fe_raviart_thomas_nodal.inst"


DEAL_II_NAMESPACE_CLOSE

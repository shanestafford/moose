//  This post processor returns the Interaction Integral
//
#include "InteractionIntegral.h"

template<>
InputParameters validParams<InteractionIntegral>()
{
  InputParameters params = validParams<ElementIntegralPostprocessor>();
  params.addCoupledVar("q", "The q function, aux variable");
  params.addCoupledVar("disp_x", "The x displacement");
  params.addCoupledVar("disp_y", "The y displacement");
  params.addCoupledVar("disp_z", "The z displacement");
  params.addRequiredParam<UserObjectName>("crack_front_definition","The CrackFrontDefinition user object name");
  params.addParam<unsigned int>("crack_front_node_index","The index of the node on the crack front corresponding to this q function");
  params.addParam<Real>("K_factor", "Conversion factor between interaction integral and stress intensity factor K");
  params.set<bool>("use_displaced_mesh") = false;
  return params;
}

InteractionIntegral::InteractionIntegral(const std::string & name, InputParameters parameters) :
    ElementIntegralPostprocessor(name, parameters),
    _grad_of_scalar_q(coupledGradient("q")),
    _crack_front_definition(&getUserObject<CrackFrontDefinition>("crack_front_definition")),
    _has_crack_front_node_index(isParamValid("crack_front_node_index")),
    _crack_front_node_index(_has_crack_front_node_index ? getParam<unsigned int>("crack_front_node_index") : 0),
    _treat_as_2d(false),
    _Eshelby_tensor(getMaterialProperty<ColumnMajorMatrix>("Eshelby_tensor")),
    _stress(getMaterialProperty<SymmTensor>("stress")),
    _strain(getMaterialProperty<SymmTensor>("elastic_strain")),
    _grad_disp_x(coupledGradient("disp_x")),
    _grad_disp_y(coupledGradient("disp_y")),
    _grad_disp_z(parameters.get<SubProblem *>("_subproblem")->mesh().dimension() == 3 ? coupledGradient("disp_z") : _grad_zero),
    _aux_stress_name(getParam<std::string>("aux_stress")),
    _aux_stress(getMaterialProperty<ColumnMajorMatrix>(_aux_stress_name)),
    _aux_disp_name(getParam<std::string>("aux_disp")),
    _aux_disp(getMaterialProperty<ColumnMajorMatrix>(_aux_disp_name)),
    _aux_grad_disp_name(getParam<std::string>("aux_grad_disp")),
    _aux_grad_disp(getMaterialProperty<ColumnMajorMatrix>(_aux_grad_disp_name)),
    _aux_strain_name(getParam<std::string>("aux_strain")),
    _aux_strain(getMaterialProperty<ColumnMajorMatrix>(_aux_strain_name)),
    _K_factor(getParam<Real>("K_factor"))
{
}

void
InteractionIntegral::initialSetup()
{
  _treat_as_2d = _crack_front_definition->treatAs2D();

  if (_treat_as_2d)
  {
    if (_has_crack_front_node_index)
    {
      mooseWarning("crack_front_node_index ignored because CrackFrontDefinition is set to treat as 2D");
    }
  }
  else
  {
    if (!_has_crack_front_node_index)
    {
      mooseError("crack_front_node_index must be specified in qFunctionJIntegral3D");
    }
  }

}

Real
InteractionIntegral::getValue()
{
  gatherSum(_integral_value);
  return _K_factor*_integral_value;
}

Real
InteractionIntegral::computeQpIntegral()
{

  RealVectorValue grad_q = _grad_of_scalar_q[_qp];

  //In the crack front coordinate system, the crack direction is (1,0,0)
  RealVectorValue crack_direction(0.0);
  crack_direction(0) = 1.0;

  ColumnMajorMatrix aux_du;
  aux_du(0,0) = _aux_grad_disp[_qp](0,0);
  aux_du(0,1) = _aux_grad_disp[_qp](0,1);
  aux_du(0,2) = _aux_grad_disp[_qp](0,2);

  ColumnMajorMatrix stress;
  stress(0,0) = _stress[_qp].xx();
  stress(0,1) = _stress[_qp].xy();
  stress(0,2) = _stress[_qp].xz();
  stress(1,0) = _stress[_qp].xy();
  stress(1,1) = _stress[_qp].yy();
  stress(1,2) = _stress[_qp].yz();
  stress(2,0) = _stress[_qp].xz();
  stress(2,1) = _stress[_qp].yz();
  stress(2,2) = _stress[_qp].zz();

  ColumnMajorMatrix strain;
  strain(0,0) = _strain[_qp].xx();
  strain(0,1) = _strain[_qp].xy();
  strain(0,2) = _strain[_qp].xz();
  strain(1,0) = _strain[_qp].xy();
  strain(1,1) = _strain[_qp].yy();
  strain(1,2) = _strain[_qp].yz();
  strain(2,0) = _strain[_qp].xz();
  strain(2,1) = _strain[_qp].yz();
  strain(2,2) = _strain[_qp].zz();

  ColumnMajorMatrix grad_disp;
  grad_disp(0,0) = _grad_disp_x[_qp](0);
  grad_disp(0,1) = _grad_disp_x[_qp](1);
  grad_disp(0,2) = _grad_disp_x[_qp](2);
  grad_disp(1,0) = _grad_disp_y[_qp](0);
  grad_disp(1,1) = _grad_disp_y[_qp](1);
  grad_disp(1,2) = _grad_disp_y[_qp](2);
  grad_disp(2,0) = _grad_disp_z[_qp](0);
  grad_disp(2,1) = _grad_disp_z[_qp](1);
  grad_disp(2,2) = _grad_disp_z[_qp](2);

  //Rotate stress, strain, and displacement to crack front coordinate system
  RealVectorValue grad_q_cf = _crack_front_definition->rotateToCrackFrontCoords(grad_q,_crack_front_node_index);
  ColumnMajorMatrix grad_disp_cf = _crack_front_definition->rotateToCrackFrontCoords(grad_disp,_crack_front_node_index);
  ColumnMajorMatrix stress_cf = _crack_front_definition->rotateToCrackFrontCoords(stress,_crack_front_node_index);
  ColumnMajorMatrix strain_cf = _crack_front_definition->rotateToCrackFrontCoords(strain,_crack_front_node_index);

  ColumnMajorMatrix dq;
  dq(0,0) = crack_direction(0)*grad_q_cf(0);
  dq(0,1) = crack_direction(0)*grad_q_cf(1);
  dq(0,2) = crack_direction(0)*grad_q_cf(2);

  //Calculate interaction integral terms

  // Term1 = stress * x1-derivative of aux disp * dq
  ColumnMajorMatrix tmp1 = dq * stress_cf;
  Real term1 = aux_du.doubleContraction(tmp1);

  // Term2 = aux stress * x1-derivative of disp * dq
  ColumnMajorMatrix tmp2 = dq * _aux_stress[_qp];
  Real term2 = grad_disp_cf(0,0)*tmp2(0,0)+grad_disp_cf(1,0)*tmp2(0,1)+grad_disp_cf(2,0)*tmp2(0,2);

  // Term3 = aux stress * strain * dq_x   (= stress * aux strain * dq_x)
  Real term3 = dq(0,0) * _aux_stress[_qp].doubleContraction(strain_cf);

  Real q_avg_seg = 1.0;
  if (!_crack_front_definition->treatAs2D())
  {
    q_avg_seg = (_crack_front_definition->getCrackFrontForwardSegmentLength(_crack_front_node_index) +
                 _crack_front_definition->getCrackFrontBackwardSegmentLength(_crack_front_node_index)) / 2.0;
  }

  Real eq = term1 + term2 - term3;

  return eq/q_avg_seg;

}

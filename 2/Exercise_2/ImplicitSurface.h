#pragma once

#ifndef IMPLICIT_SURFACE_H
#define IMPLICIT_SURFACE_H

#include "Eigen.h"
#include "SimpleMesh.h"

class ImplicitSurface
{
public:
    virtual double Eval(const Eigen::Vector3d &x) = 0;
};

// Sphere::Eval:   f(x,y,z) = x² + y² + z² – R²
class Sphere : public ImplicitSurface
{
public:
    Sphere(const Eigen::Vector3d &center, double radius) : m_center(center), m_radius(radius)
    {
    }

    double Eval(const Eigen::Vector3d &_x)
    {
        // TODO--: implement the implicit sphere formula using the member variables m_center and m_radius
        Eigen::Vector3d p = _x - m_center;
        return p.squaredNorm() - m_radius * m_radius;
    }

private:
    Eigen::Vector3d m_center;
    double m_radius;
};

class Torus : public ImplicitSurface
{
public:
    Torus(const Eigen::Vector3d &center, double radius, double a) : m_center(center), m_radius(radius), m_a(a)
    {
    }

    // Torus::Eval:   f(x,y,z) = (x² + y² + z² + R² – a²)² – 4 R² (x² + y²)
    double Eval(const Eigen::Vector3d &_x)
    {
        Eigen::Vector3d p = _x - m_center;
        double x2y2 = p.x() * p.x() + p.y() * p.y();
        double sum2 = p.squaredNorm() + m_radius * m_radius - m_a * m_a;
        return sum2 * sum2 - 4.0 * m_radius * m_radius * x2y2;
    }

private:
    Eigen::Vector3d m_center;
    double m_radius;
    double m_a;
};

class Hoppe : public ImplicitSurface
{
public:
    Hoppe(const std::string &filenamePC)
    {
        m_pointcloud.ReadFromFile(filenamePC);
    }

    double Eval(const Eigen::Vector3d &_x)
    {
        Eigen::Vector3f x = Eigen::Vector3f((float)_x.x(), (float)_x.y(), (float)_x.z());
        unsigned int idx = m_pointcloud.GetClosestPoint(x);
        if (idx == m_pointcloud.GetPoints().size())
            return 0.0;

        Eigen::Vector3f p = m_pointcloud.GetPoints()[idx];
        Eigen::Vector3f n = m_pointcloud.GetNormals()[idx];

        double value = (x - p).dot(n);

        // TODO--: implement the evaluation using Hoppe's method (see lecture slides)
        return value;
    }

private:
    PointCloud m_pointcloud;
};

///////////////////////////////////////////

class FunctionSamples
{
public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW

    FunctionSamples() {}

    void insertSample(const Vector3d &pos, const double targetFunctionValue)
    {
        m_pos.push_back(pos);
        m_val.push_back(targetFunctionValue);
    }

    std::vector<Vector3d> m_pos;
    std::vector<double> m_val;
};

class RBF : public ImplicitSurface
{
public:
    RBF(const std::string &filenamePC)
    {
        // load point cloud
        m_pointcloud.ReadFromFile(filenamePC);

        // Create function samples
        double eps = 0.01f;

        // on surface points (-> center points of the RBFs)
        for (unsigned int i = 0; i < m_pointcloud.GetPoints().size(); i++)
        {
            Eigen::Vector3f ptF = m_pointcloud.GetPoints()[i];
            Eigen::Vector3f nF = m_pointcloud.GetNormals()[i];
            const Vector3d pt(ptF[0], ptF[1], ptF[2]);
            const Vector3d n(nF[0], nF[1], nF[2]);

            m_funcSamp.insertSample(pt, 0); // on surface point => distance = 0
        }

        // off surface points
        for (unsigned int i = 0; i < m_pointcloud.GetPoints().size(); i++)
        {
            Eigen::Vector3f ptF = m_pointcloud.GetPoints()[i];
            Eigen::Vector3f nF = m_pointcloud.GetNormals()[i];
            const Vector3d pt(ptF[0], ptF[1], ptF[2]);
            const Vector3d n(nF[0], nF[1], nF[2]);

            m_funcSamp.insertSample(pt + n * eps, eps); // off surface point => distance = eps
            eps *= -1;
        }

        m_numCenters = (unsigned int)m_pointcloud.GetPoints().size();
        const unsigned int dim = m_numCenters + 4;

        // build and solve the linear system of equations
        m_systemMatrix = MatrixXd(dim, dim);
        m_rhs = VectorXd(dim);
        m_coefficents = VectorXd(dim); // result of the linear system
        BuildSystem();
        SolveSystem();
    }

    double Eval(const Eigen::Vector3d &_x)
    {
        // TODO: eval the RBF function based on the coefficents stored in m_coefficents
        // the first m_numCenters entries contain the coefficients of the kernels (that can be evaluated using EvalBasis())
        // the following parameters are the coeffients for the linear and the constant part
        // the centers of the RBFs are the first m_numCenters sample points (use m_funcSamp.m_pos[i] to access them)
        // hint: Eigen provides a norm() function to compute the l2-norm of a vector (e.g. see macro phi(i,j))
        Vector3d p = _x;
        double result = 0.0;
        // RBF kernel terms
        for (int i = 0; i < m_numCenters; i++)
        {
            double r = (p - m_funcSamp.m_pos[i]).norm();
            result += m_coefficents[i] * EvalBasis(r); // alpha_i * ||p_i - x||^3
        }

        // Linear and constant terms
        double beta0 = m_coefficents[m_numCenters];
        double betax = m_coefficents[m_numCenters + 1];
        double betay = m_coefficents[m_numCenters + 2];
        double betaz = m_coefficents[m_numCenters + 3];

        result += beta0 + betax * p.x() + betay * p.y() + betaz * p.z();

        return result;
    }

private:
    double EvalBasis(double x)
    {
        return x * x * x;
    }

#define phi(i, j) EvalBasis((m_funcSamp.m_pos[i] - m_funcSamp.m_pos[j]).norm())

    //! Computes the system matrix.
    void BuildSystem()
    {
        unsigned int N = m_funcSamp.m_pos.size();
        MatrixXd A(2 * m_numCenters, m_numCenters + 4);
        VectorXd b(2 * m_numCenters);
        A.setZero();
        b.setZero();

        // TODO: fill the matrix A and the vector b as described in the exercise sheet
        // note that all sample points (both on and off surface points) are stored in m_funcSamp
        // you can access matrix elements using for example A(i,j) for the i-th row and j-th column
        // similar you access the elements of the vector b, e.g. b(i) for the i-th element

        for (unsigned int i = 0; i < N; ++i)
        {
            Vector3d xi = m_funcSamp.m_pos[i];

            for (unsigned int j = 0; j < m_numCenters; ++j)
            {
                double r = (xi - m_funcSamp.m_pos[j]).norm();
                A(i, j) = EvalBasis(r);
            }

            A(i, m_numCenters + 0) = 1.0;
            A(i, m_numCenters + 1) = xi.x();
            A(i, m_numCenters + 2) = xi.y();
            A(i, m_numCenters + 3) = xi.z();

            b(i) = m_funcSamp.m_val[i];
        }

        // build the system matrix and the right hand side of the normal equation
        m_systemMatrix = A.transpose() * A;
        m_rhs = A.transpose() * b;

        // regularizer -> smoother surface
        // pushes the coefficients to zero
        double lambda = 0.0001;
        m_systemMatrix.diagonal() += lambda * lambda * VectorXd::Ones(m_numCenters + 4);
    }

    void SolveSystem()
    {
        std::cerr << "Solving RBF System" << std::endl;
        std::cerr << "Computing LU..." << std::endl;

        FullPivLU<Matrix<double, Dynamic, Dynamic>> LU(m_systemMatrix);
        m_coefficents = LU.solve(m_rhs);

        std::cerr << "Done." << std::endl;
    }

    // point cloud
    PointCloud m_pointcloud;

    //! The given function samples (at each function sample, we place a basis function).
    FunctionSamples m_funcSamp;

    //! The number of center = number of function samples.
    unsigned int m_numCenters;

    //! the right hand side of our system of linear equation. Unfortunately, float-precision is not enough, so we have to use double here.
    VectorXd m_rhs;

    //! the system matrix. Unfortunately, float-precision is not enough, so we have to use double here
    MatrixXd m_systemMatrix;

    //! store the result of the linear system here. Unfortunately, float-precision is not enough, so we have to use double here
    VectorXd m_coefficents;
};

#endif // IMPLICIT_SURFACE_H

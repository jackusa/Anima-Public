#pragma once

#include "animaLTSWTransformAgregator.h"
#include <animaLogRigid3DTransform.h>
#include <animaLinearTransformEstimationTools.h>

#include <algorithm>
#include <itkExceptionObject.h>

namespace anima
{

template <unsigned int NDimensions>
LTSWTransformAgregator <NDimensions>::
LTSWTransformAgregator() : Superclass()
{
    m_LTSCut = 0.5;
    m_StoppingThreshold = 1.0e-2;
    m_EstimationBarycenter.Fill(0);
}

template <unsigned int NDimensions>
typename LTSWTransformAgregator <NDimensions>::PointType
LTSWTransformAgregator <NDimensions>::
GetEstimationBarycenter()
{
    return m_EstimationBarycenter;
}

template <unsigned int NDimensions>
bool
LTSWTransformAgregator <NDimensions>::
Update()
{
    this->SetUpToDate(false);
    bool returnValue = false;

    if (this->GetInputWeights().size() != this->GetInputTransforms().size())
        return false;

    switch (this->GetInputTransformType())
    {
        case Superclass::TRANSLATION:
        case Superclass::DIRECTIONAL_AFFINE:
            if ((this->GetInputWeights().size() != this->GetInputOrigins().size())||
                    (this->GetInputTransforms().size() != this->GetInputOrigins().size()))
                return false;

            returnValue = this->ltswEstimateTranslationsToAny();
            this->SetUpToDate(returnValue);
            return returnValue;

        case Superclass::RIGID:
        case Superclass::ANISOTROPIC_SIM:
        case Superclass::AFFINE:
            returnValue = this->ltswEstimateAnyToAffine();
            return returnValue;

        default:
            throw itk::ExceptionObject(__FILE__, __LINE__,"Specific LTSW agregation not handled yet...",ITK_LOCATION);
    }
}

template <unsigned int NDimensions>
bool
LTSWTransformAgregator <NDimensions>::
ltswEstimateTranslationsToAny()
{
    unsigned int nbPts = this->GetInputOrigins().size();

    if (NDimensions > 3)
        throw itk::ExceptionObject(__FILE__, __LINE__,"Dimension not supported",ITK_LOCATION);

    std::vector <PointType> originPoints(nbPts);
    std::vector <PointType> transformedPoints(nbPts);
    std::vector <double> weights = this->GetInputWeights();

    BaseInputTransformType * currTrsf = 0;
    if (this->GetOutputTransformType() == Superclass::ANISOTROPIC_SIM)
        currTrsf = this->GetCurrentLinearTransform();

    for (unsigned int i = 0; i < nbPts; ++i)
    {
        PointType tmpOrig = this->GetInputOrigin(i);
        BaseInputTransformType * tmpTrsf = this->GetInputTransform(i);
        PointType tmpDisp = tmpTrsf->TransformPoint(tmpOrig);
        originPoints[i] = tmpOrig;
        if (this->GetOutputTransformType() == Superclass::ANISOTROPIC_SIM)
            transformedPoints[i] = currTrsf->TransformPoint(tmpDisp);
        else
            transformedPoints[i] = tmpDisp;
    }
    
    vnl_matrix <ScalarType> covPcaOriginPoints(NDimensions, NDimensions, 0);
    if (this->GetOutputTransformType() == Superclass::ANISOTROPIC_SIM)
    {
        itk::Matrix<ScalarType, NDimensions, NDimensions> emptyMatrix;
        emptyMatrix.Fill(0);

        if (this->GetOrthogonalDirectionMatrix() != emptyMatrix)
        {
            covPcaOriginPoints = this->GetOrthogonalDirectionMatrix().GetVnlMatrix().as_matrix();
        }
        else
        {
            itk::Point <ScalarType, NDimensions> unweightedBarX;
            vnl_matrix <ScalarType> covOriginPoints(NDimensions, NDimensions, 0);
            for (unsigned int i = 0; i < nbPts; ++i)
            {
                for (unsigned int j = 0; j < NDimensions; ++j)
                    unweightedBarX[j] += originPoints[i][j] / nbPts;
            }
            for (unsigned int i = 0; i < nbPts; ++i)
            {
                for (unsigned int j = 0; j < NDimensions; ++j)
                {
                    for (unsigned int k = 0; k < NDimensions; ++k)
                        covOriginPoints(j, k) += (originPoints[i][j] - unweightedBarX[j])*(originPoints[i][k] - unweightedBarX[k]);
                }
            }
            itk::SymmetricEigenAnalysis < vnl_matrix <ScalarType>, vnl_diag_matrix<ScalarType>, vnl_matrix <ScalarType> > eigenSystem(3);
            vnl_diag_matrix <double> eValsCov(NDimensions);
            eigenSystem.SetOrderEigenValues(true);
            eigenSystem.ComputeEigenValuesAndVectors(covOriginPoints, eValsCov, covPcaOriginPoints);
            /* return eigen vectors in row !!!!!!! */
            covPcaOriginPoints = covPcaOriginPoints.transpose();
            if (vnl_determinant(covPcaOriginPoints) < 0)
                covPcaOriginPoints *= -1.0;
        }
    }

    std::vector <PointType> originPointsFiltered = originPoints;
    std::vector <PointType> transformedPointsFiltered = transformedPoints;
    std::vector <double> weightsFiltered = weights;

    std::vector < std::pair <unsigned int, double> > residualErrors;
    PointType tmpOutPoint;
    itk::Vector <double,3> tmpDiff;

    bool continueLoop = true;
    unsigned int numMaxIter = 100;
    unsigned int num_itr = 0;

    typename BaseOutputTransformType::Pointer resultTransform, resultTransformOld;

    while(num_itr < numMaxIter)
    {
        ++num_itr;

        switch (this->GetOutputTransformType())
        {
            case Superclass::TRANSLATION:
                anima::computeTranslationLSWFromTranslations<InternalScalarType,ScalarType,NDimensions>
                        (originPointsFiltered,transformedPointsFiltered,weightsFiltered,resultTransform);
                break;

            case Superclass::RIGID:
                anima::computeRigidLSWFromTranslations<InternalScalarType,ScalarType,NDimensions>
                        (originPointsFiltered,transformedPointsFiltered,weightsFiltered,resultTransform);
                break;

            case Superclass::ANISOTROPIC_SIM:
                m_EstimationBarycenter = anima::computeAnisotropSimLSWFromTranslations<InternalScalarType, ScalarType, NDimensions>
                    (originPointsFiltered, transformedPointsFiltered, weightsFiltered, resultTransform, covPcaOriginPoints);
                break;

            case Superclass::AFFINE:
                m_EstimationBarycenter = anima::computeAffineLSWFromTranslations<InternalScalarType,ScalarType,NDimensions>
                        (originPointsFiltered,transformedPointsFiltered,weightsFiltered,resultTransform);
                break;

            default:
                throw itk::ExceptionObject(__FILE__, __LINE__,"Not implemented yet...",ITK_LOCATION);
        }

        continueLoop = endLTSCondition(resultTransformOld,resultTransform);

        if (!continueLoop)
            break;

        resultTransformOld = resultTransform;
        residualErrors.clear();
        for (unsigned int i = 0;i < nbPts;++i)
        {
            if (weights[i] <= 0)
                continue;

            tmpOutPoint = resultTransform->TransformPoint(originPoints[i]);
            tmpDiff = tmpOutPoint - transformedPoints[i];
            residualErrors.push_back(std::make_pair (i,tmpDiff.GetNorm()));
        }

        unsigned int numLts = floor(residualErrors.size() * m_LTSCut);

        std::vector < std::pair <unsigned int, double> >::iterator begIt = residualErrors.begin();
        std::vector < std::pair <unsigned int, double> >::iterator sortPart = begIt + numLts;

        std::partial_sort(begIt,sortPart,residualErrors.end(),anima::errors_pair_comparator());

        originPointsFiltered.resize(numLts);
        transformedPointsFiltered.resize(numLts);
        weightsFiltered.resize(numLts);

        for (unsigned int i = 0;i < numLts;++i)
        {
            originPointsFiltered[i] = originPoints[residualErrors[i].first];
            transformedPointsFiltered[i] = transformedPoints[residualErrors[i].first];
            weightsFiltered[i] = weights[residualErrors[i].first];
        }
    }

    this->SetOutput(resultTransform);
    return true;
}

template <unsigned int NDimensions>
bool
LTSWTransformAgregator <NDimensions>::
ltswEstimateAnyToAffine()
{
    if ((this->GetInputTransformType() == Superclass::AFFINE)&&(this->GetOutputTransformType() == Superclass::RIGID))
        throw itk::ExceptionObject(__FILE__, __LINE__,"Agregation from affine transforms to rigid is not supported yet...",ITK_LOCATION);

    typedef itk::MatrixOffsetTransformBase <InternalScalarType, NDimensions> BaseMatrixTransformType;
    typedef anima::LogRigid3DTransform <InternalScalarType> LogRigidTransformType;

    unsigned int nbPts = this->GetInputTransforms().size();
    std::vector <InternalScalarType> weights = this->GetInputWeights();

    std::vector < vnl_matrix <InternalScalarType> > logTransformations(nbPts);
    vnl_matrix <InternalScalarType> tmpMatrix(NDimensions+1,NDimensions+1,0), tmpLogMatrix(NDimensions+1,NDimensions+1,0);
    tmpMatrix(NDimensions,NDimensions) = 1;
    typename BaseMatrixTransformType::MatrixType affinePart;
    itk::Vector <InternalScalarType, NDimensions> offsetPart;

    for (unsigned int i = 0;i < nbPts;++i)
    {
        if (this->GetInputTransformType() == Superclass::AFFINE)
        {
            BaseMatrixTransformType *tmpTrsf = (BaseMatrixTransformType *)this->GetInputTransform(i);
            affinePart = tmpTrsf->GetMatrix();
            offsetPart = tmpTrsf->GetOffset();

            for (unsigned int j = 0;j < NDimensions;++j)
            {
                tmpMatrix(j,NDimensions) = offsetPart[j];
                for (unsigned int k = 0;k < NDimensions;++k)
                    tmpMatrix(j,k) = affinePart(j,k);
            }

            logTransformations[i] = anima::GetLogarithm(tmpMatrix);
            if (!std::isfinite(logTransformations[i](0,0)))
            {
                logTransformations[i].fill(0);
                this->SetInputWeight(i,0);
            }
        }
        else
        {
            LogRigidTransformType *tmpTrsf = (LogRigidTransformType *)this->GetInputTransform(i);
            logTransformations[i] = tmpTrsf->GetLogTransform();
        }
    }

    std::vector < vnl_matrix <InternalScalarType> > logTransformationsFiltered = logTransformations;
    std::vector <InternalScalarType> weightsFiltered = weights;

    // For LTS
    std::vector < PointType > originPoints(nbPts);
    std::vector < PointType > transformedPoints(nbPts);

    for (unsigned int i = 0;i < nbPts;++i)
    {
        PointType tmpOrig = this->GetInputOrigin(i);
        BaseInputTransformType * tmpTrsf = this->GetInputTransform(i);
        PointType tmpDisp = tmpTrsf->TransformPoint(tmpOrig);
        originPoints[i] = tmpOrig;
        transformedPoints[i] = tmpDisp;
    }

    std::vector < std::pair <unsigned int, double> > residualErrors;

    bool continueLoop = true;
    unsigned int numMaxIter = 100;
    unsigned int num_itr = 0;

    typename BaseOutputTransformType::Pointer resultTransform, resultTransformOld;

    while(num_itr < numMaxIter)
    {
        ++num_itr;

        anima::computeLogEuclideanAverage<InternalScalarType,ScalarType,NDimensions>(logTransformationsFiltered,weightsFiltered,resultTransform);
        continueLoop = endLTSCondition(resultTransformOld,resultTransform);

        if (!continueLoop)
            break;

        resultTransformOld = resultTransform;
        residualErrors.clear();

        BaseMatrixTransformType *tmpTrsf = (BaseMatrixTransformType *)resultTransform.GetPointer();

        for (unsigned int i = 0;i < nbPts;++i)
        {
            if (weights[i] <= 0)
                continue;

            double tmpDiff = 0;
            PointType tmpDisp = tmpTrsf->TransformPoint(originPoints[i]);

            for (unsigned int j = 0;j < NDimensions;++j)
                tmpDiff += (transformedPoints[i][j] - tmpDisp[j]) * (transformedPoints[i][j] - tmpDisp[j]);

            residualErrors.push_back(std::make_pair (i,tmpDiff));
        }

        unsigned int numLts = floor(residualErrors.size() * m_LTSCut);

        std::vector < std::pair <unsigned int, double> >::iterator begIt = residualErrors.begin();
        std::vector < std::pair <unsigned int, double> >::iterator sortPart = begIt + numLts;

        std::partial_sort(begIt,sortPart,residualErrors.end(),anima::errors_pair_comparator());

        logTransformationsFiltered.resize(numLts);
        weightsFiltered.resize(numLts);

        for (unsigned int i = 0;i < numLts;++i)
        {
            logTransformationsFiltered[i] = logTransformations[residualErrors[i].first];
            weightsFiltered[i] = weights[residualErrors[i].first];
        }
    }

    this->SetOutput(resultTransform);
    return true;
}

template <unsigned int NDimensions>
bool
LTSWTransformAgregator <NDimensions>::
endLTSCondition(BaseOutputTransformType *oldTrsf, BaseOutputTransformType *newTrsf)
{
    if (oldTrsf == NULL)
        return true;

    typename BaseOutputTransformType::ParametersType oldParams = oldTrsf->GetParameters();
    typename BaseOutputTransformType::ParametersType newParams = newTrsf->GetParameters();

    for (unsigned int i = 0;i < newParams.GetSize();++i)
    {
        double diffParam = fabs(newParams[i] - oldParams[i]);
        if (diffParam > m_StoppingThreshold)
            return true;
    }

    return false;
}

} // end of namespace anima

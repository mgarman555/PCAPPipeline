#include "LiveLinkViconUtils.h"

namespace LiveLinkViconUtils
{

  bool GetMarkerTranslations(const TArray<float>& PropertyValues, TArray<FVector>& MarkerData)
  {
    // Markers are in the format [n, x1, y1, z1 ... xn, yn, zn, 0, 0, 0 ... , 0, 0, 0]
    MarkerData.Empty();
    if (PropertyValues.Num() % 3 != 0)
    {
      UE_LOG(LogLiveLinkViconUtils, Log, TEXT("Cannot get marker translations, property values are in invalid format"));
      return false;
    }
    unsigned int PropertyCount = PropertyValues.Num();
    //const unsigned int NumMarkers = static_cast<unsigned int>(PropertyValues[0]);
    for (unsigned int i = 0; i < PropertyCount; i = i + 3)
    {
      MarkerData.Emplace(FVector(PropertyValues[i], PropertyValues[i + 1], PropertyValues[i + 2]));
    }
    return true;
  }

}

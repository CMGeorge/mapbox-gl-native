package com.mapbox.mapboxsdk.maps;


import android.graphics.RectF;
import android.support.annotation.NonNull;
import android.support.annotation.Nullable;
import android.support.v4.util.LongSparseArray;

import com.mapbox.mapboxsdk.annotations.Annotation;
import com.mapbox.mapboxsdk.annotations.BaseMarkerOptions;
import com.mapbox.mapboxsdk.annotations.BaseMarkerViewOptions;
import com.mapbox.mapboxsdk.annotations.Icon;
import com.mapbox.mapboxsdk.annotations.Marker;
import com.mapbox.mapboxsdk.annotations.MarkerView;
import com.mapbox.mapboxsdk.annotations.MarkerViewManager;

import java.util.ArrayList;
import java.util.List;

class MarkersFunctions implements Markers {

  private final NativeMapView nativeMapView;
  private final LongSparseArray<Annotation> annotations;
  private final IconManager iconManager;
  private final MarkerViewManager markerViewManager;

  MarkersFunctions(NativeMapView nativeMapView, LongSparseArray<Annotation> annotations, IconManager iconManager,
                   MarkerViewManager markerViewManager) {
    this.nativeMapView = nativeMapView;
    this.annotations = annotations;
    this.iconManager = iconManager;
    this.markerViewManager = markerViewManager;
  }

  @Override
  public Marker addBy(@NonNull BaseMarkerOptions markerOptions, @NonNull MapboxMap mapboxMap) {
    Marker marker = prepareMarker(markerOptions);
    long id = nativeMapView != null ? nativeMapView.addMarker(marker) : 0;
    marker.setMapboxMap(mapboxMap);
    marker.setId(id);
    annotations.put(id, marker);
    return marker;
  }

  @Override
  public List<Marker> addBy(@NonNull List<? extends BaseMarkerOptions> markerOptionsList, @NonNull MapboxMap
    mapboxMap) {
    int count = markerOptionsList.size();
    List<Marker> markers = new ArrayList<>(count);
    if (count > 0) {
      BaseMarkerOptions markerOptions;
      Marker marker;
      for (int i = 0; i < count; i++) {
        markerOptions = markerOptionsList.get(i);
        marker = prepareMarker(markerOptions);
        markers.add(marker);
      }

      if (markers.size() > 0) {
        long[] ids = null;
        if (nativeMapView != null) {
          ids = nativeMapView.addMarkers(markers);
        }

        long id = 0;
        Marker m;
        for (int i = 0; i < markers.size(); i++) {
          m = markers.get(i);
          m.setMapboxMap(mapboxMap);
          if (ids != null) {
            id = ids[i];
          } else {
            // unit test
            id++;
          }
          m.setId(id);
          annotations.put(id, m);
        }

      }
    }
    return markers;
  }

  @Override
  public void update(@NonNull Marker updatedMarker, @NonNull MapboxMap mapboxMap) {
    if (updatedMarker == null) {
      return;
    }

    if (updatedMarker.getId() == -1) {
      return;
    }

    if (!(updatedMarker instanceof MarkerView)) {
      iconManager.ensureIconLoaded(updatedMarker, mapboxMap);
    }

    nativeMapView.updateMarker(updatedMarker);

    int index = annotations.indexOfKey(updatedMarker.getId());
    if (index > -1) {
      annotations.setValueAt(index, updatedMarker);
    }
  }

  @Override
  public List<Marker> obtainAll() {
    List<Marker> markers = new ArrayList<>();
    Annotation annotation;
    for (int i = 0; i < annotations.size(); i++) {
      annotation = annotations.get(annotations.keyAt(i));
      if (annotation instanceof Marker) {
        markers.add((Marker) annotation);
      }
    }
    return markers;
  }

  @Override
  public List<Marker> obtainAllIn(@NonNull RectF rectangle) {
    // convert Rectangle to be density depedent
    float pixelRatio = nativeMapView.getPixelRatio();
    RectF rect = new RectF(rectangle.left / pixelRatio,
      rectangle.top / pixelRatio,
      rectangle.right / pixelRatio,
      rectangle.bottom / pixelRatio);

    long[] ids = nativeMapView.queryPointAnnotations(rect);

    List<Long> idsList = new ArrayList<>(ids.length);
    for (long id : ids) {
      idsList.add(id);
    }

    List<Marker> annotations = new ArrayList<>(ids.length);
    List<Annotation> annotationList = obtainAnnotations();
    int count = annotationList.size();
    for (int i = 0; i < count; i++) {
      Annotation annotation = annotationList.get(i);
      if (annotation instanceof com.mapbox.mapboxsdk.annotations.Marker && idsList.contains(annotation.getId())) {
        annotations.add((com.mapbox.mapboxsdk.annotations.Marker) annotation);
      }
    }

    return new ArrayList<>(annotations);
  }

  @Override
  public MarkerView addViewBy(@NonNull BaseMarkerViewOptions markerOptions, @NonNull MapboxMap mapboxMap, @Nullable
    MarkerViewManager.OnMarkerViewAddedListener onMarkerViewAddedListener) {
    final MarkerView marker = prepareViewMarker(markerOptions);

    // add marker to map
    marker.setMapboxMap(mapboxMap);
    long id = nativeMapView.addMarker(marker);
    marker.setId(id);
    annotations.put(id, marker);

    if (onMarkerViewAddedListener != null) {
      markerViewManager.addOnMarkerViewAddedListener(marker, onMarkerViewAddedListener);
    }
    markerViewManager.setEnabled(true);
    markerViewManager.setWaitingForRenderInvoke(true);
    return marker;
  }

  @Override
  public List<MarkerView> addViewsBy(@NonNull List<? extends BaseMarkerViewOptions> markerViewOptions, @NonNull
    MapboxMap mapboxMap) {
    List<MarkerView> markers = new ArrayList<>();
    for (BaseMarkerViewOptions markerViewOption : markerViewOptions) {
      // if last marker
      if (markerViewOptions.indexOf(markerViewOption) == markerViewOptions.size() - 1) {
        // get notified when render occurs to invalidate and draw MarkerViews
        markerViewManager.setWaitingForRenderInvoke(true);
      }
      // add marker to map
      MarkerView marker = prepareViewMarker(markerViewOption);
      marker.setMapboxMap(mapboxMap);
      long id = nativeMapView.addMarker(marker);
      marker.setId(id);
      annotations.put(id, marker);
      markers.add(marker);
    }
    markerViewManager.setEnabled(true);
    markerViewManager.update();
    return markers;
  }

  @Override
  public List<MarkerView> obtainViewsIn(@NonNull RectF rectangle) {
    float pixelRatio = nativeMapView.getPixelRatio();
    RectF rect = new RectF(rectangle.left / pixelRatio,
      rectangle.top / pixelRatio,
      rectangle.right / pixelRatio,
      rectangle.bottom / pixelRatio);

    long[] ids = nativeMapView.queryPointAnnotations(rect);

    List<Long> idsList = new ArrayList<>(ids.length);
    for (long id : ids) {
      idsList.add(id);
    }

    List<MarkerView> annotations = new ArrayList<>(ids.length);
    List<Annotation> annotationList = obtainAnnotations();
    int count = annotationList.size();
    for (int i = 0; i < count; i++) {
      Annotation annotation = annotationList.get(i);
      if (annotation instanceof MarkerView && idsList.contains(annotation.getId())) {
        annotations.add((MarkerView) annotation);
      }
    }

    return new ArrayList<>(annotations);
  }

  @Override
  public void reload() {
    iconManager.reloadIcons();
    int count = annotations.size();
    for (int i = 0; i < count; i++) {
      Annotation annotation = annotations.get(i);
      if (annotation instanceof Marker) {
        Marker marker = (Marker) annotation;
        nativeMapView.removeAnnotation(annotation.getId());
        long newId = nativeMapView.addMarker(marker);
        marker.setId(newId);
      }
    }
  }

  private Marker prepareMarker(BaseMarkerOptions markerOptions) {
    Marker marker = markerOptions.getMarker();
    Icon icon = iconManager.loadIconForMarker(marker);
    marker.setTopOffsetPixels(iconManager.getTopOffsetPixelsForIcon(icon));
    return marker;
  }

  private MarkerView prepareViewMarker(BaseMarkerViewOptions markerViewOptions) {
    MarkerView marker = markerViewOptions.getMarker();
    iconManager.loadIconForMarkerView(marker);
    return marker;
  }

  private List<Annotation> obtainAnnotations() {
    List<Annotation> annotations = new ArrayList<>();
    for (int i = 0; i < this.annotations.size(); i++) {
      annotations.add(this.annotations.get(this.annotations.keyAt(i)));
    }
    return annotations;
  }
}
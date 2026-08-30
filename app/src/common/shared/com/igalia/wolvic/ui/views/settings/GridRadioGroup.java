package com.igalia.wolvic.ui.views.settings;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.widget.RadioGroup;

/** A compact two-column RadioGroup used for larger groups of short options. */
public class GridRadioGroup extends RadioGroup {
    private static final int COLUMN_COUNT = 2;
    private final int mColumnGap;
    private final int mRowGap;

    public GridRadioGroup(Context context) {
        this(context, null);
    }

    public GridRadioGroup(Context context, AttributeSet attrs) {
        super(context, attrs);
        float density = context.getResources().getDisplayMetrics().density;
        mColumnGap = Math.round(12.0f * density);
        mRowGap = Math.round(4.0f * density);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int availableWidth = Math.max(0,
                MeasureSpec.getSize(widthMeasureSpec) - getPaddingLeft() - getPaddingRight());
        int columnWidth = Math.max(0,
                (availableWidth - mColumnGap * (COLUMN_COUNT - 1)) / COLUMN_COUNT);
        int rows = (getChildCount() + COLUMN_COUNT - 1) / COLUMN_COUNT;
        int[] rowHeights = new int[rows];

        for (int i = 0; i < getChildCount(); i++) {
            View child = getChildAt(i);
            if (child.getVisibility() == GONE) {
                continue;
            }
            child.measure(
                    MeasureSpec.makeMeasureSpec(columnWidth, MeasureSpec.EXACTLY),
                    getChildMeasureSpec(heightMeasureSpec,
                            getPaddingTop() + getPaddingBottom(),
                            child.getLayoutParams().height));
            int row = i / COLUMN_COUNT;
            rowHeights[row] = Math.max(rowHeights[row], child.getMeasuredHeight());
        }

        int desiredHeight = getPaddingTop() + getPaddingBottom();
        for (int rowHeight : rowHeights) {
            desiredHeight += rowHeight;
        }
        if (rows > 1) {
            desiredHeight += mRowGap * (rows - 1);
        }

        setMeasuredDimension(
                resolveSize(availableWidth + getPaddingLeft() + getPaddingRight(), widthMeasureSpec),
                resolveSize(desiredHeight, heightMeasureSpec));
    }

    @Override
    protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
        int availableWidth = right - left - getPaddingLeft() - getPaddingRight();
        int columnWidth = Math.max(0,
                (availableWidth - mColumnGap * (COLUMN_COUNT - 1)) / COLUMN_COUNT);
        int rows = (getChildCount() + COLUMN_COUNT - 1) / COLUMN_COUNT;
        int[] rowHeights = new int[rows];
        for (int i = 0; i < getChildCount(); i++) {
            View child = getChildAt(i);
            if (child.getVisibility() != GONE) {
                int row = i / COLUMN_COUNT;
                rowHeights[row] = Math.max(rowHeights[row], child.getMeasuredHeight());
            }
        }

        int y = getPaddingTop();
        for (int row = 0; row < rows; row++) {
            for (int column = 0; column < COLUMN_COUNT; column++) {
                int index = row * COLUMN_COUNT + column;
                if (index >= getChildCount()) {
                    break;
                }
                View child = getChildAt(index);
                if (child.getVisibility() == GONE) {
                    continue;
                }
                int x = getPaddingLeft() + column * (columnWidth + mColumnGap);
                child.layout(x, y, x + columnWidth, y + child.getMeasuredHeight());
            }
            y += rowHeights[row] + mRowGap;
        }
    }
}

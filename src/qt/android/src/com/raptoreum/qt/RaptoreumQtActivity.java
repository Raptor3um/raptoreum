package com.raptoreum.qt;

import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;

import org.qtproject.qt5.android.bindings.QtActivity;

import java.io.File;

public class RaptoreumQtActivity extends QtActivity
{
	@Override
	public void onCreate(Bundle savedInstanceState)
	{
		final File raptoreumDir = new File(getFilesDir().getAbsolutePath() + "/.raptoreumcore");
		if (!raptoreumDir.exists()) {
			raptoreumDir.mkdir();
		}


		super.onCreate(savedInstanceState);
	}
}